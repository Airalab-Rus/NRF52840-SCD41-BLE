#pragma once

#include <Arduino.h>

/**
 * АКБ Li-Ion через делитель на P0.31 (AIN7).
 *
 * Схема:  BAT+ --- R_top(1 МОм) ---+--- P0.31 (AIN7)
 *                                   |
 *                              R_bot(1.5 МОм)
 *                                   |
 *                                  GND
 *
 * Номиналы задаются в platformio.ini (BIRDY_VBAT_R_TOP / BIRDY_VBAT_R_BOT, в омах).
 * Vbat = Vpin * (R_top + R_bot) / R_bot
 */

void batteryBegin(void);
/** Напряжение АКБ в мВ (усреднение 8 отсчётов, компенсация делителя). */
uint16_t batteryReadMv(void);
/** Напряжение на пине в мВ (без компенсации) — для диагностики. */
uint16_t batteryReadPinMv(void);
/** Процент заряда по кривой Li-Ion (3.3 В = 0 %, 4.2 В = 100 %). */
uint8_t batteryPercent(uint16_t mv);

/** Поправка делителя/ADC в процентах (по мультиметру), 50..200. */
void batterySetCalPct(uint8_t pct);
uint8_t batteryGetCalPct(void);
