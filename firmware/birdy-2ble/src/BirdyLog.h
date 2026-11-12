#pragma once

#include <Arduino.h>

/**
 * Лог измерений в памяти платы: три уровня агрегации.
 *
 *   RAW  — каждый замер (1 мин), 120 записей  → ~2 часа
 *   HOUR — средние за час,        480 записей → ~20 суток
 *   DAY  — средние за сутки,      200 записей → ~полгода
 *
 * Все уровни — кольцевые буферы в RAM, периодически сбрасываются в файлы
 * `/log.raw`, `/log.h`, `/log.d` на InternalFS (LittleFS, регион 0xED000).
 *
 * Запись 12 байт (little-endian):
 *   u32 ts   — локальное время (unix + смещение часового пояса), сек
 *   u16 co2  — ppm
 *   i16 temp — 0.01 °C
 *   u16 hum  — 0.01 %
 *   u16 mv   — напряжение АКБ, мВ
 */
#define BIRDY_LOG_RAW_MAX  120
#define BIRDY_LOG_HOUR_MAX 480
#define BIRDY_LOG_DAY_MAX  200

enum BirdyLogTier : uint8_t { LOG_TIER_RAW = 0, LOG_TIER_HOUR = 1, LOG_TIER_DAY = 2, LOG_TIER_NUM = 3 };

struct __attribute__((packed)) BirdyLogRec {
  uint32_t ts;
  uint16_t co2;
  int16_t  temp;
  uint16_t hum;
  uint16_t mv;
};

/** Загрузить логи из flash (после InternalFS.begin()). */
void logBegin(void);

/** Добавить замер (внутри обновляются все три уровня). */
bool logAppend(uint16_t co2, float tempC, float humPct, uint16_t mv);

/** Установить время (unix + смещение пояса, сек). */
void logSetEpoch(uint32_t epoch);
uint32_t logNow(void);
bool logTimeValid(void);

uint16_t logCount(uint8_t tier);
bool logGet(uint8_t tier, uint16_t idx, BirdyLogRec *out);
void logClear(void);
void logFlush(void);

/** Отметить успешную загрузку (сбрасывает счётчик «страховки»). */
void logMarkAlive(void);
