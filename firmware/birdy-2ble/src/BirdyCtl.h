#pragma once

#include <Arduino.h>

/**
 * 2BLE — модуль удалённого управления «птичкой» по BLE GATT.
 *
 * Зачем: BTHome — односторонний (HA только слушает рекламу). Чтобы менять
 * порог/углы/калибровку без перепрошивки, добавлен отдельный GATT-сервис.
 * Реклама на время окна делается CONNECTABLE, поэтому подключаться могут
 * два телефона одновременно (Bluefruit.begin(2, 0)).
 *
 * Сервис 0xFFF0 «Birdy Control»:
 *   0xFFF1 cmd    WRITE  [cmd, arg0, arg1, ...]
 *   0xFFF2 status NOTIFY [code, co2_lo, co2_hi, flags, thr_lo, thr_hi]
 *   0xFFF3 info   READ   12 байт: версия, порог, углы, темп, звук, dead, CO2, гистерезис
 */

/* Команды (первый байт записи в 0xFFF1) */
enum BirdyCmd : uint8_t {
  /* 0x01/0x02 свободны: калибровка CO2 УБРАНА (только заводская). */
  BCMD_SET_THRESH  = 0x03, /* arg: ppm lo, hi -> порог «смерти» */
  BCMD_SET_ALIVE   = 0x04, /* arg: int8 угол «жива» (-100..100) */
  BCMD_SET_DEAD    = 0x05, /* arg: int8 угол «мёртва» */
  BCMD_SET_TRANS   = 0x06, /* arg: сек хода серво (0..30) */
  BCMD_FORCE_MEAS  = 0x07, /* немедленный замер + отправка BTHome */
  BCMD_RESET       = 0x08, /* сброс настроек к дефолтам */
  BCMD_SOUND       = 0x09, /* arg: 0/1 звук */
  BCMD_CHIRP_UP    = 0x0A, /* тест: восходящая трель */
  BCMD_CHIRP_DOWN  = 0x0B, /* тест: нисходящая трель */
  BCMD_SERVO_SWEEP = 0x0C, /* тест: свип серво */
  BCMD_GOTO        = 0x0D, /* arg: int8 — сразу перейти в положение (-100..100), для подбора углов */
  BCMD_SET_TOFF    = 0x0E, /* arg: int8 — смещение температуры в 0.1 °C (-50..50) */
};

/* Коды ответа (первый байт notify в 0xFFF2) */
enum BirdyStatus : uint8_t {
  BST_OK           = 0,
  BST_UNKNOWN_CMD  = 1,
  BST_BAD_ARG      = 2,
  BST_CAL_TOO_HIGH = 3,
  BST_SENSOR_FAIL  = 4,
};

/** Настройки, которые можно менять по BLE (живут в NVS). */
struct BirdyRuntime {
  uint16_t co2Bad;      /* порог «смерти», ppm */
  uint16_t hystPpm;     /* гистерезис оживления, ppm */
  int8_t   angleAlive;  /* угол «жива», -100..100 % */
  int8_t   angleDead;   /* угол «мёртва» */
  uint8_t  transitionSec;
  uint8_t  soundEnabled;
  uint8_t  chirpUp;
  uint8_t  chirpDown;
  uint8_t  chirpUpVar;
  uint8_t  chirpDownVar;
  uint8_t  batCalPct;   /* поправка АКБ, % (по мультиметру) */
  int16_t  toffTenths;  /* смещение температуры SCD41, 0.1 °C (-50..50) */
  uint16_t cfgVer;      /* версия конфига (для миграций) */
};

/** Обратные вызовы в main.cpp — модуль не знает про датчик/серво/пьезо. */
struct BirdyHooks {
  void (*applySettings)(void);        /* переприменить настройки (угол, темп, звук, t-offset) */
  void (*chirp)(bool up);             /* проиграть трель (тест) */
  void (*sweep)(void);                /* свип серво (тест) */
  void (*moveTo)(int percent);        /* сразу поставить серво в положение % */
};

void birdyCtlBegin(const BirdyHooks &hooks);
BirdyRuntime &birdyRun(void);

/** Вызывать после каждого замера: обновляет status/info (vbatMv — напряжение АКБ). */
void birdyCtlReport(uint16_t co2ppm, bool sensorOk, bool dead, uint16_t vbatMv = 0);

/** Забрать флаг «сделать замер сейчас» (однократный). */
bool birdyCtlTakeForceMeasure(void);

/** Отправить код ответа в status всем подключённым (для отложенных команд). */
void birdyCtlReportCode(uint8_t code);

/** Текстовый ответ (характеристика 0xFFF5) во все подключения. */
void birdyCtlText(const char *msg);

/** Открыть окно connectable-рекламы для подключения телефонов. */
void birdyCtlOpenWindow(uint16_t seconds);
/** Тик окна: закрывает рекламу по таймауту, если нет подключений. */
void birdyCtlTick(void);

void birdyCtlSave(void);
void birdyCtlReset(void);
