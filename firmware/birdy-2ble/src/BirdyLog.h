#pragma once

#include <Arduino.h>

/**
 * Лог измерений в памяти платы.
 *
 * Кольцевой буфер на 256 записей (по 12 байт) живёт в RAM и периодически
 * сбрасывается в файл `/birdy.log` на InternalFS (LittleFS, регион 0xED000).
 * Так flash не изнашивается: полная перезапись 3 КБ раз в 30 замеров.
 *
 * Запись (12 байт, little-endian):
 *   u16 co2      — ppm
 *   i16 temp     — 0.01 °C
 *   u16 hum      — 0.01 %
 *   u16 mv       — напряжение АКБ, мВ
 *   u32 up       — секунды с загрузки (для привязки к времени на телефоне)
 */
#define BIRDY_LOG_MAX 256

struct __attribute__((packed)) BirdyLogRec {
  uint16_t co2;
  int16_t  temp;
  uint16_t hum;
  uint16_t mv;
  uint32_t up;
};

/** Загрузить лог из flash (вызывать один раз после InternalFS.begin()). */
void logBegin(void);

/** Добавить запись; раз в 30 штук — автосохранение в flash. */
bool logAppend(uint16_t co2, float tempC, float humPct, uint16_t mv);

/** Количество записей в кольце (0..BIRDY_LOG_MAX). */
uint16_t logCount(void);

/** Запись по индексу 0..count-1 (0 = самая старая). */
bool logGet(uint16_t idx, BirdyLogRec *out);

/** Очистить лог. */
void logClear(void);

/** Принудительно сохранить в flash. */
void logFlush(void);
