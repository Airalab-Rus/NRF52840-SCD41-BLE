/*
 * CO2 AirNode — nRF52840 SuperMini (Nice!Nano) + SCD41, BTHome v2 over BLE.
 *
 * Архитектура и BLE-стек — 1-в-1 по рабочему эталону
 *   https://github.com/oxyphil-x/CO2-BTHome-sensor (BTHome.cpp/BTHome.h
 *   без изменений; board = nicenano, тот же Adafruit core, тот же SparkFun SCD4x).
 *
 * Пины (реальная распайка на нашей плате):
 *   SCD41 SDA = P0.17, SCL = P0.20.
 *
 * Память:
 *   0x00000-0x25FFF — UF2-бутлоадер + SoftDevice S140 v6 (уже на месте),
 *   0x026000-...    — это приложение.
 *
 * ============================================================= FIX 29.10 ====
 * Корневая причина «В HA статично 400 ppm / 0.0 °C / 0.0 %»:
 *   while (!mySensor.readMeasurement()) { ... }   // УСПЕХ: данные уже в глобалах библиотеки
 *   if (mySensor.readMeasurement()) { ... }        // ← ЛИШНИЙ второй read: буфер low-power
 *                                                   //   уже пуст, следующий результат через
 *                                                   //   ~1.5 с → readMeasurement() вернул false
 *                                                   //   → got=false → в отчёт ушли значения по
 *                                                   //   умолчанию (400/0.0/0.0)
 * Исправление: читаем глобалы библиотеки НАПРЯМУЮ, как в эталоне — без
 * повторного readMeasurement(). Это 1-в-1 совпадает с работающим oxyphil-x.
 * Дополнительно: первый замер сразу после boot (быстрое подтверждение данных),
 * и лог «RAW sensor: co2=.. temp=.. rh=..» — чтобы видеть реальные числа.
 * ================================================================
 */
#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include "BTHome.h"

#define MY_SDA_PIN 17 /* P0.17 — SDA SCD41 */
#define MY_SCL_PIN 20 /* P0.20 — SCL SCD41 */

#define DEVICE_NAME "CO2-AirNode"
#define SCD4x_ALTITUDE 160          // м над уровнем моря (точность CO2)
#define SCD4x_TEMP_OFFSET 0         // сдвиг температуры, °C
#define MEASUREMENT_PERIOD 60       // сек между циклами замера/отчёта

SCD4x mySensor;
BTHome bthome;

float temp = 0.0f;
float hum = 0.0f;
uint16_t co2 = 400;
uint8_t packet_id = 0;
uint32_t lastCycleMs = 0;

bool doMeasure();
void sendReport();

void setup()
{
	Serial.begin(115200);
	delay(500);
	Serial.println("=== CO2-AirNode boot ===");

	/* BLE-стек (внутри Bluefruit.begin() — как в эталоне). */
	bthome.begin(DEVICE_NAME, false, "", false);
	Serial.println("[boot] BLE begin done");

	/* I2C на наших пинах + датчик. */
	Wire.setPins(MY_SDA_PIN, MY_SCL_PIN);
	Wire.begin();
	bool ok = mySensor.begin(false, false, false);
	Serial.printf("[boot] SCD41 begin: %s\n", ok ? "OK" : "FAILED");
	if (ok) {
		mySensor.setSensorAltitude(SCD4x_ALTITUDE);
		mySensor.setTemperatureOffset(SCD4x_TEMP_OFFSET);
	}

	/* Первый замер сразу после boot — быстрое подтверждение реальных данных. */
	bool got = doMeasure();
	Serial.printf("[boot] first measure: %s co2=%u temp=%.1fC rh=%.1f%%\n",
	              got ? "OK" : "FAIL", co2, temp, hum);
	sendReport();
	lastCycleMs = millis();
}

/* Замер: low-power periodic -> readMeasurement -> stop (1-в-1 эталон oxyphil-x).
 *
 * ВАЖНО (FIX): while(!readMeasurement()) уже читает данные в глобалы
 * библиотеки (co2/temperature/humidity). НЕТ повторного readMeasurement() —
 * он падал и отбрасывал реальное значение (буфер low-power пуст между
 * обновлениями). Читаем глобалы напрямую. */
bool doMeasure()
{
	bool got = false;
	Wire.begin();

	bool started = mySensor.startLowPowerPeriodicMeasurement();
	Serial.printf("[meas] startLowPower: %s\n", started ? "OK" : "FAIL(stuck/bus)");

	delay(4500); /* как в эталоне: дать датчику начать первый цикл */

	/* Ждём готовности данных и ЧИТАЕМ: успешный readMeasurement() кладёт
	 * CO2/T/RH в глобалы библиотеки. */
	uint32_t t0 = millis();
	bool readOK = false;
	while (millis() - t0 < 120000UL) {
		if (mySensor.readMeasurement()) { readOK = true; break; }
		delay(250);
	}
	Serial.printf("[meas] read: %s (readyIn=%dms)\n", readOK ? "OK" : "FAIL",
	              (int)(millis() - t0));

	if (readOK) {
		/* Читаем глобалы библиотеки — БЕЗ повторного readMeasurement(). */
		uint16_t c = (uint16_t)mySensor.getCO2();
		float t = mySensor.getTemperature();
		float h = mySensor.getHumidity();
		Serial.printf("[meas] RAW sensor: co2=%u ppm temp=%.2fC rh=%.2f%%\n", c, t, h);

		/* Санк-чек: отбрасываем заведомо мёртвые/мусорные значения,
		 * чтобы не отправить 0 или 65535 в HA. */
		if (c >= 100 && c <= 5000 && t >= -40.0f && t <= 60.0f && h >= 0.0f && h <= 100.0f) {
			co2 = constrain(c, 400, 2000);
			temp = t;
			hum = h;
			got = true;
		} else {
			Serial.println("[meas] values out of range -> discarded (keeping last good)");
		}
	}

	mySensor.stopPeriodicMeasurement(0);
	Wire.end();

	Serial.printf("[meas] measure: %s co2=%u temp=%.1fC rh=%.1f%%\n",
	              got ? "OK" : "FAIL", co2, temp, hum);
	return got;
}

void sendReport()
{
	bthome.resetMeasurement();
	bthome.addMeasurement(ID_PACKET, packet_id++);
	bthome.addMeasurement(ID_TEMPERATURE_PRECISE, temp); /* 0.01 C */
	bthome.addMeasurement(ID_HUMIDITY_PRECISE, hum);     /* 0.01 % */
	bthome.addMeasurement(ID_CO2, co2);                  /* ppm */
	bthome.addMeasurement(ID_BATTERY, (uint8_t)100);     /* делитель АКБ не распаян: условно 100% */
	bthome.addMeasurement_state(STATE_BATTERY_LOW, 0);
	bthome.sendPacket(500);
}

void loop()
{
	if (millis() - lastCycleMs >= (uint32_t)MEASUREMENT_PERIOD * 1000UL) {
		lastCycleMs = millis();
		doMeasure();
		sendReport();
	}
	delay(200);
}
