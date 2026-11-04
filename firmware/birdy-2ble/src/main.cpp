/*
 * CO2-Birdy 2BLE — nRF52840 SuperMini + SCD41 + серво + пьезо + BLE-управление.
 *
 * Отличия от варианта BIRDY (папка ../BIRDY):
 *   - BLE-реклама CONNECTABLE (BTHome-данные сохраняются), максимум 2 подключения
 *     (Bluefruit.begin(2, 0)) — телефон и планшет одновременно;
 *   - GATT-сервис «Birdy Control» (0xFFF0): калибровка SCD41 (SPC), порог CO2,
 *     углы серво, время хода, звук, тесты — без перепрошивки;
 *   - настройки живут в NVS (InternalFS, файл /birdy.cfg);
 *   - окно connectable-рекламы 30 с после каждого замера (энергосбережение).
 *
 * Пины:
 *   SCD41 SDA = P0.17, SCL = P0.20
 *   серво     = P0.24 (BIRDY_SERVO_PIN)
 *   пьезо     = P0.22 (BIRDY_PIEZO_PIN)
 */
#include <Arduino.h>
#include <bluefruit.h>   /* BLEDfu, enterUf2Dfu */
#include <Wire.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include "BTHome.h"
#include "BirdConfig.h"
#include "BirdServo.h"
#include "BirdPiezo.h"
#include "BirdyCtl.h"
#include "battery.h"

#define MY_SDA_PIN 17               /* P0.17 — SDA SCD41 */
#define MY_SCL_PIN 20               /* P0.20 — SCL SCD41 */

#define DEVICE_NAME "CO2-Birdy"
#define SCD4x_ALTITUDE 160          /* м над уровнем моря (точность CO2) */
#define SCD4x_TEMP_OFFSET 0         /* сдвиг температуры, °C */
#define MEASUREMENT_PERIOD 60       /* сек между циклами замера/отчёта */
#define BIRDY_ADV_WINDOW_SEC 30     /* окно connectable-рекламы, сек */

SCD4x mySensor;
BTHome bthome;
BLEDfu bledfu;      /* OTA DFU по BLE: телефон переводит плату в бутлоадер сам */
BirdServo birdyServo;
BirdPiezo birdyPiezo;

float temp = 0.0f;
float hum = 0.0f;
uint16_t co2 = 400;
uint16_t vbatMv = 0; /* напряжение АКБ, мВ (P0.31) */
uint8_t  vbatPct = 0;
uint8_t packet_id = 0;
uint32_t lastCycleMs = 0;

static bool birdDead = false;
static uint8_t deadTicks = 0;
static uint8_t aliveTicks = 0;

bool doMeasure();
void sendReport(bool haveData);
void updateBirdState(uint16_t co2ppm, bool valid);
static void servoCalibrationSweep(void);
static void i2cBusRecovery(void);
static bool sensorReinit(void);

/* ============================ BLE-хуки (BirdyCtl) ============================ */

/* Отложенные задачи из BLE-колбэка: I2C нельзя трогать из BLE-потока
 * одновременно с замером — поэтому ставим задачу, исполняем в loop(). */
static volatile bool g_pendingSweep = false;
/* Калибровка CO2 УБРАНА: датчик калиброван на заводе (AFC сам это делает).
 * Вместо неё — только смещение температуры (toff) и настройки птички. */

/* Переприменить настройки из NVS к железу. */
static void hookApplySettings(void) {
	BirdyRuntime &r = birdyRun();
	birdyServo.setTransitionMs((uint32_t)r.transitionSec * 1000UL);
	batterySetCalPct(r.batCalPct);
	/* Сразу показываем новое положение ТЕКУЩЕГО состояния: иначе угол меняется,
	 * а серво стоит до следующего перехода (и кажется, что настройка не работает). */
	birdyServo.writePercent(birdDead ? r.angleDead : r.angleAlive);
	Serial.printf("[ctl] применено: thr=%u hyst=%u alive=%d dead=%d sec=%u snd=%u batcal=%u\n",
	              r.co2Bad, r.hystPpm, (int)r.angleAlive, (int)r.angleDead,
	              r.transitionSec, r.soundEnabled, r.batCalPct);
}

/* goto=<%> — сразу поставить серво в положение (подбор углов на глаз). */
static void hookMoveTo(int percent) {
	Serial.printf("[birdy] goto %d%%\n", percent);
	birdyServo.writePercent(percent);
}

/* Трель НЕ запускаем из BLE-потока (гонка с tick() из loop()) — только флаг. */
static volatile uint8_t g_pendingChirp = 0; /* 1 = вверх, 2 = вниз */

static void hookChirp(bool up) {
	g_pendingChirp = up ? 1 : 2;
}

static void hookSweep(void) {
	g_pendingSweep = true; /* плавный тест ~12 с — выполняем в loop(), не в BLE-потоке */
}

/* OTA: переход в DFU-бутлоадер. Выполняем в loop(): enterUf2Dfu() не возвращается. */
static volatile bool g_pendingDfu = false;
static void hookDfu(void) { g_pendingDfu = true; }

/* Плавный ход с ожиданием (easeInOut из BirdServo, шаг 10 мс). */
static void servoMoveBlocking(int percent, uint32_t maxMs) {
	birdyServo.writePercent(percent);
	uint32_t t0 = millis();
	while (birdyServo.isMoving() && (millis() - t0) < maxMs) {
		birdyServo.tick();
		birdyPiezo.tick();
		delay(10);
	}
}

/* Плавный тест серво: медленно в один край -> в другой -> в середину -> чирик.
 * Без рывков: каждый ход — easeInOut за BIRDY_SELFTEST_MS (по умолчанию 4 с). */
static void servoCalibrationSweep(void) {
	BirdyRuntime &r = birdyRun();
	const uint32_t keepMs = (uint32_t)r.transitionSec * 1000UL;
	const uint32_t moveMs = (uint32_t)BIRDY_SELFTEST_MS;

	Serial.printf("[birdy] плавный тест серво: -%d%% -> +%d%% -> 0%% (%u мс на ход)\n",
	              (int)BIRDY_SELFTEST_EXTREME, (int)BIRDY_SELFTEST_EXTREME, (unsigned)moveMs);

	birdyServo.setTransitionMs(moveMs);
	servoMoveBlocking(-BIRDY_SELFTEST_EXTREME, moveMs + 3000UL);
	servoMoveBlocking(+BIRDY_SELFTEST_EXTREME, moveMs + 3000UL);
	servoMoveBlocking(0, moveMs + 3000UL);
	birdyServo.setTransitionMs(keepMs);

	/* Чирик в конце теста (полностью отыгрываем здесь же). */
	if (r.soundEnabled) {
		birdyPiezo.playChirp(true, r.chirpUp, r.chirpUpVar);
		uint32_t t0 = millis();
		while (birdyPiezo.isPlaying() && (millis() - t0) < 3000UL) {
			birdyPiezo.tick();
			delay(5);
		}
	}
	Serial.println("[birdy] тест серво завершён");
}

/* ============================ Машина состояний «птички» ============================
 * co2 >= порог (BIRDY_STATUS_DEAD_TICKS подряд) -> «смерть»: угол dead + низкая трель;
 * co2 < порог - гистерезис (REVIVE_TICKS)       -> «оживление»: угол alive + восходящая.
 * Все параметры — из NVS, меняются по BLE.
 * ============================================================================ */
void updateBirdState(uint16_t co2ppm, bool valid) {
	BirdyRuntime &s = birdyRun();

	if (valid) {
		const bool bad = (co2ppm >= s.co2Bad);
		const bool good = ((uint32_t)co2ppm + s.hystPpm < s.co2Bad);

		if (bad) {
			deadTicks = (deadTicks < 250) ? deadTicks + 1 : 250;
			aliveTicks = 0;
		} else if (good) {
			aliveTicks = (aliveTicks < 250) ? aliveTicks + 1 : 250;
			deadTicks = 0;
		}

		Serial.printf("[birdy] cycle: co2=%u порог=%u dead=%d ticks(d=%u/a=%u)\n",
		              co2ppm, s.co2Bad, birdDead ? 1 : 0, (unsigned)deadTicks,
		              (unsigned)aliveTicks);

		if (!birdDead && bad && deadTicks >= BIRDY_STATUS_DEAD_TICKS) {
			birdDead = true;
			Serial.printf("[birdy] DEATH: co2=%u ppm >= %u\n", co2ppm, s.co2Bad);
			birdyServo.writePercent(s.angleDead);
			if (s.soundEnabled) birdyPiezo.playChirp(false, s.chirpDown, s.chirpDownVar);
		} else if (birdDead && good && aliveTicks >= BIRDY_STATUS_REVIVE_TICKS) {
			birdDead = false;
			Serial.printf("[birdy] REVIVE: co2=%u ppm < %u\n", co2ppm,
			              (unsigned)(s.co2Bad - s.hystPpm));
			birdyServo.writePercent(s.angleAlive);
			if (s.soundEnabled) birdyPiezo.playChirp(true, s.chirpUp, s.chirpUpVar);
		}
	}

	birdyCtlReport(co2ppm, valid, birdDead, vbatMv);
}

/* ================================== setup ================================== */
void setup() {
	Serial.begin(115200);
	delay(500);
	Serial.println("=== CO2-Birdy 2BLE boot ===");

	/* BLE: BTHome-реклама (connectable) + 2 подключения. */
	bthome.begin(DEVICE_NAME, false, "", false);
	Serial.println("[boot] BLE begin (2 conn)");

	/* OTA DFU-сервис: должен стартовать раньше остальных сервисов. */
	bledfu.begin();
	Serial.println("[boot] BLEDfu (OTA) готов");

	/* Управление: NVS + GATT-сервис 0xFFF0. */
	BirdyHooks hooks;
	hooks.applySettings = hookApplySettings;
	hooks.chirp = hookChirp;
	hooks.sweep = hookSweep;
	hooks.moveTo = hookMoveTo;
	hooks.enterDfu = hookDfu;
	birdyCtlBegin(hooks);

	/* АКБ: делитель на P0.31. */
	batteryBegin();

	/* I2C на наших пинах + датчик. */
	Wire.setPins(MY_SDA_PIN, MY_SCL_PIN);
	Wire.begin();
	bool ok = mySensor.begin(false, false, false);
	Serial.printf("[boot] SCD41 begin: %s\n", ok ? "OK" : "FAILED");
	if (ok) {
		mySensor.setSensorAltitude(SCD4x_ALTITUDE);
		mySensor.setTemperatureOffset((float)birdyRun().toffTenths / 10.0f);
	}

	/* Один раз после обновления прошивки — заводской сброс SCD41:
	 * убирает сдвиг нуля от прежних экспериментов с калибровкой CO2,
	 * то есть возвращает штатные (заводские) показания. */
	if (ok && birdyRun().cfgVer < 2) {
		Serial.println("[scd] первичный заводской сброс датчика");
		mySensor.performFactoryReset(1200);
		sensorReinit();
		birdyRun().cfgVer = 2;
		birdyCtlSave();
		Serial.println("[scd] сброс выполнен, показания штатные");
	}

	/* Серво + пьезо. */
	BirdyRuntime &r = birdyRun();
	birdyServo.begin();
	hookApplySettings(); /* темп хода из NVS (в begin() он берётся из дефолтов) */
	birdyPiezo.begin();
#if BIRDY_SELFTEST
	servoCalibrationSweep();
#endif
	birdyServo.writePercent(r.angleAlive);

	/* Первый замер сразу после boot. */
	bool got = doMeasure();
	Serial.printf("[boot] first measure: %s co2=%u temp=%.1fC rh=%.1f%%\n",
	              got ? "OK" : "FAIL", co2, temp, hum);
	sendReport(got);
	updateBirdState(co2, got);
	birdyCtlOpenWindow(BIRDY_ADV_WINDOW_SEC);
	lastCycleMs = millis();
}

/* ------------------------ восстановление I2C и датчика ------------------------ */

/* 9 тактов SCL освобождают SDA, если датчик остался посреди передачи
 * (именно так он «замолкал» после Wire.end() в середине обмена). */
static void i2cBusRecovery(void) {
	Serial.println("[i2c] восстановление шины");
	Wire.end();
	pinMode(MY_SDA_PIN, INPUT_PULLUP);
	pinMode(MY_SCL_PIN, OUTPUT);
	for (int i = 0; i < 9; i++) {
		digitalWrite(MY_SCL_PIN, LOW);
		delayMicroseconds(5);
		digitalWrite(MY_SCL_PIN, HIGH);
		delayMicroseconds(5);
	}
	/* STOP-условие */
	pinMode(MY_SDA_PIN, OUTPUT);
	digitalWrite(MY_SDA_PIN, LOW);
	delayMicroseconds(5);
	digitalWrite(MY_SCL_PIN, HIGH);
	delayMicroseconds(5);
	digitalWrite(MY_SDA_PIN, HIGH);
	delayMicroseconds(5);
	Wire.setPins(MY_SDA_PIN, MY_SCL_PIN);
	Wire.begin();
	delay(20);
}

/* Полная переинициализация SCD41 (после сбоя обмена или калибровки). */
static bool sensorReinit(void) {
	Serial.println("[scd] re-init датчика");
	i2cBusRecovery();
	bool ok = mySensor.begin(false, false, false);
	Serial.printf("[scd] begin: %s\n", ok ? "OK" : "FAIL");
	if (ok) {
		mySensor.setSensorAltitude(SCD4x_ALTITUDE);
		mySensor.setTemperatureOffset(SCD4x_TEMP_OFFSET);
	}
	return ok;
}

/* ================================== замер ==================================
 * ФИКС зависаний: шину I2C между циклами НЕ выключаем (Wire.end не вызываем),
 * таймаут сокращён до 45 с, внутри ожидания «тикают» серво и пьезо — иначе
 * трель зависает на одной ноте. При сбое — восстановление шины + re-init. */
/* Периодическое измерение держим ЗАПУЩЕННЫМ постоянно: только в этом режиме
 * работает автокалибровка SCD41 (ASC). Раньше start/stop на каждом цикле
 * сбрасывал её таймер, и базовый уровень CO2 мог уплывать. */
static bool g_sensorRunning = false;

static void sensorStart(void) {
	if (g_sensorRunning) return;
	Wire.begin();
	if (!mySensor.startLowPowerPeriodicMeasurement()) {
		mySensor.stopPeriodicMeasurement(500);
		sensorReinit();
		if (!mySensor.startLowPowerPeriodicMeasurement()) {
			Serial.println("[scd] start: FAIL");
			return;
		}
	}
	g_sensorRunning = true;
	Serial.println("[scd] периодическое измерение запущено (ASC активна)");
	delay(4000); /* первый цикл low-power — 30 с, даём старт */
}

static void sensorStop(void) {
	if (!g_sensorRunning) return;
	mySensor.stopPeriodicMeasurement(500);
	g_sensorRunning = false;
}

bool doMeasure() {
	bool got = false;
	static uint8_t failStreak = 0;
	Wire.begin();

	/* Смещение температуры датчик принимает только в idle-режиме,
	 * поэтому при изменении — останавливаем, пишем, запускаем снова. */
	{
		static int16_t appliedToff = 32767;
		const int16_t want = birdyRun().toffTenths;
		if (want != appliedToff) {
			sensorStop();
			mySensor.setTemperatureOffset((float)want / 10.0f);
			appliedToff = want;
			Serial.printf("[scd] смещение температуры = %.1f C\n", want / 10.0f);
		}
	}

	sensorStart();
	if (!g_sensorRunning) {
		if (++failStreak >= 3) { failStreak = 0; sensorReinit(); }
		return false;
	}

	uint32_t t0 = millis();
	bool readOK = false;
	while (millis() - t0 < 45000UL) {
		if (mySensor.readMeasurement()) {
			readOK = true;
			break;
		}
		/* тикаем серво/пьезо — иначе трель «залипает» на одной ноте */
		birdyServo.tick();
		birdyPiezo.tick();
		delay(100);
	}
	Serial.printf("[meas] read: %s (readyIn=%dms)\n", readOK ? "OK" : "FAIL",
	              (int)(millis() - t0));

	if (readOK) {
		/* Только глобалы библиотеки — БЕЗ повторного readMeasurement(). */
		uint16_t c = (uint16_t)mySensor.getCO2();
		float t = mySensor.getTemperature();
		float h = mySensor.getHumidity();
		Serial.printf("[meas] RAW sensor: co2=%u ppm temp=%.2fC rh=%.2f%%\n", c, t, h);

		/* Без зажима снизу: раньше `constrain(c, 400, 2000)` превращал любые
		 * значения ниже 400 в ровно 400 ppm — невозможно было отличить сбой
		 * датчика от реальных ~380 ppm. */
		if (c >= 0 && c <= 5000 && t >= -40.0f && t <= 60.0f && h >= 0.0f && h <= 100.0f) {
			co2 = c;
			temp = t;
			hum = h;
			got = true;
		} else {
			Serial.println("[meas] values out of range -> discarded (keeping last good)");
		}
	}

	/* Датчик НЕ останавливаем — пусть работает непрерывно (ASC).
	 * Wire.end() тоже НЕ вызываем — это и ломало шину. */

	/* Эскалация лечения, если замеры не проходят (без выдёргивания питания). */
	if (got) {
		failStreak = 0;
	} else {
		failStreak++;
		sensorStop();
		if (failStreak == 3) {
			sensorReinit(); /* переинициализация шины и датчика */
		} else if (failStreak == 5) {
			/* Последнее средство — заводской сброс датчика (сбрасывает и SPC-сдвиг,
			 * то есть лечит и «испорченную калибровку»). */
			Serial.println("[scd] factory reset датчика");
			mySensor.performFactoryReset(1200);
			sensorReinit();
		} else if (failStreak >= 8) {
			failStreak = 0;
			sensorReinit();
		}
	}

	Serial.printf("[meas] measure: %s co2=%u temp=%.1fC rh=%.1f%%\n",
	              got ? "OK" : "FAIL", co2, temp, hum);
	return got;
}

/* ================================ BTHome-отчёт ================================ */
void sendReport(bool haveData) {
	bthome.resetMeasurement();
	bthome.addMeasurement(ID_PACKET, packet_id++);
	/* Если замер не удался — НЕ отправляем фиктивные 400/0.0/0.0.
	 * HA тогда честно покажет устаревшие данные вместо ложных. */
	if (haveData) {
		bthome.addMeasurement(ID_TEMPERATURE_PRECISE, temp);
		bthome.addMeasurement(ID_HUMIDITY_PRECISE, hum);
		bthome.addMeasurement(ID_CO2, co2);
	}

	/* АКБ: реальные мВ и % (делитель на P0.31). */
	vbatMv = batteryReadMv();
	vbatPct = batteryPercent(vbatMv);
	const uint16_t pinMv = batteryReadPinMv();
	Serial.printf("[bat] pin=%u mV -> vbat=%u mV, %u%%%s\n", pinMv, vbatMv, vbatPct,
	              (vbatMv < BIRDY_VBAT_LOW_MV) ? "  LOW!" : "");
	bthome.addMeasurement(ID_VOLTAGE, vbatMv / 1000.0f); /* 0.001 В */
	bthome.addMeasurement(ID_BATTERY, vbatPct);
	bthome.addMeasurement_state(STATE_BATTERY_LOW, (vbatMv < BIRDY_VBAT_LOW_MV) ? 1 : 0);
	bthome.sendPacket(500);
}

/* ================================== loop ================================== */
void loop() {
	bool cycled = false;

	/* Отложенные команды из BLE — в основном потоке. */
	if (g_pendingSweep) {
		g_pendingSweep = false;
		servoCalibrationSweep();
		birdyCtlText("ok sweep");
	}

	/* OTA: команда dfu — уходим в бутлоадер (без двойного RESET). */
	if (g_pendingDfu) {
		g_pendingDfu = false;
		Serial.println("[dfu] переход в DFU-бутлоадер");
		birdyCtlText("ok dfu");
		delay(300); /* дать уйти ответу */
		enterUf2Dfu();
	}

	/* Трель по команде из BLE — запускаем в основном потоке. */
	if (g_pendingChirp) {
		const uint8_t c = g_pendingChirp;
		g_pendingChirp = 0;
		BirdyRuntime &rr = birdyRun();
		if (rr.soundEnabled) {
			birdyPiezo.playChirp(c == 1, c == 1 ? rr.chirpUp : rr.chirpDown,
			                     c == 1 ? rr.chirpUpVar : rr.chirpDownVar);
		}
	}

	if (birdyCtlTakeForceMeasure()) {
		Serial.println("[ctl] принудительный замер по BLE");
		cycled = true;
	} else if (millis() - lastCycleMs >= (uint32_t)MEASUREMENT_PERIOD * 1000UL) {
		cycled = true;
	}

	if (cycled) {
		lastCycleMs = millis();
		bool got = doMeasure();
		sendReport(got);
#if BIRDY_MODE_GAUGE
		if (got) {
			int pct = (int)((long)(co2 - 400) * 200 / 800) - 100;
			if (pct < -100) pct = -100;
			if (pct > 100) pct = 100;
			birdyServo.writePercent(pct);
			Serial.printf("[birdy] gauge: co2=%u -> %d%%\n", co2, pct);
		}
#endif
		updateBirdState(co2, got);
		birdyCtlOpenWindow(BIRDY_ADV_WINDOW_SEC);
	}

	birdyServo.tick();
	birdyPiezo.tick();
	birdyCtlTick();
	delay(4); /* мелкий шаг: иначе ноты трели растягиваются */
}
