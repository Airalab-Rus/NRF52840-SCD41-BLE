#pragma once

#include <stdint.h>

/**
 * BIRDY — «птичка» для CO2 AirNode (nRF52840 SuperMini).
 *
 * Логика 1-в-1 как в Birdie (ESP32, Zigbee):
 *   CO2 >= порог (co2Bad, дефолт 1200 ppm) — подтверждено N тиков
 *       -> птица «умирает»: серво -> angleDead, пьезо — нисходящая трель.
 *   CO2 < порог - гистерезис (100 ppm) — подтверждено M тиков
 *       -> птица «оживает»: серво -> angleAlive, пьезо — восходящая трель.
 *
 * Настройки — build_flags в platformio.ini (см. ниже), дефолты ниже.
 */

/* ======================== Дефолты (перекрываются -D в platformio.ini) ============ */
#ifndef BIRDY_SERVO_PIN
#define BIRDY_SERVO_PIN 24 /* P0.24 — серво (свободный GPIO) */
#endif
#ifndef BIRDY_PIEZO_PIN
#define BIRDY_PIEZO_PIN 22 /* P0.22 — пьезо (свободный GPIO) */
#endif
#ifndef BIRDY_CO2_BAD
#define BIRDY_CO2_BAD 1200U
#endif
#ifndef BIRDY_ANGLE_ALIVE
#define BIRDY_ANGLE_ALIVE 0
#endif
#ifndef BIRDY_ANGLE_DEAD
#define BIRDY_ANGLE_DEAD 100
#endif
#ifndef BIRDY_TRANSITION_SEC
#define BIRDY_TRANSITION_SEC 3U
#endif
#ifndef BIRDY_SOUND_ENABLED
#define BIRDY_SOUND_ENABLED 1U
#endif
#ifndef BIRDY_CHIRP_UP
#define BIRDY_CHIRP_UP 0U
#endif
#ifndef BIRDY_CHIRP_DOWN
#define BIRDY_CHIRP_DOWN 0U
#endif
#ifndef BIRDY_CHIRP_UP_VAR
#define BIRDY_CHIRP_UP_VAR 0U
#endif
#ifndef BIRDY_CHIRP_DOWN_VAR
#define BIRDY_CHIRP_DOWN_VAR 0U
#endif
/* ---- АКБ: делитель на P0.31 (AIN7), два резистора по 10 МОм (SMD 1005) ---- */
#ifndef BIRDY_VBAT_PIN
#define BIRDY_VBAT_PIN 31
#endif
#ifndef BIRDY_VBAT_R_TOP
#define BIRDY_VBAT_R_TOP 10000000UL /* верхний, к плюсу АКБ (1005 = 10 МОм) */
#endif
#ifndef BIRDY_VBAT_R_BOT
#define BIRDY_VBAT_R_BOT 10000000UL /* нижний, к GND (1005 = 10 МОм) */
#endif
/* Порог «глубокого разряда» (мВ) — ниже него в BTHome уходит battery_low. */
#ifndef BIRDY_VBAT_LOW_MV
#define BIRDY_VBAT_LOW_MV 3300U
#endif
/* Поправка (%), если ADC занижает из-за высокоомного делителя: подбирается
 * по мультиметру (истинное мВ / показанное мВ * 100). По умолчанию 100. */
#ifndef BIRDY_VBAT_CAL_PCT
#define BIRDY_VBAT_CAL_PCT 100U
#endif

/* Self-test серво при старте: плавный ход -100% -> +100% -> 0% + чирик. */
#ifndef BIRDY_SELFTEST
#define BIRDY_SELFTEST 1
#endif
/* Длительность одного хода теста (мс) и крайние положения теста (%). */
#ifndef BIRDY_SELFTEST_MS
#define BIRDY_SELFTEST_MS 8000U
#endif
#ifndef BIRDY_SELFTEST_EXTREME
#define BIRDY_SELFTEST_EXTREME 100
#endif

/* Режим:
 *   0 — «птичка» (alive/dead по порогу, как в Birdie) — основной;
 *   1 — ДИАГНОСТИКА: серво = стрелка CO2 (400 ppm -> -100 %, 1200 ppm -> +100 %).*/
#ifndef BIRDY_MODE_GAUGE
#define BIRDY_MODE_GAUGE 0
#endif

/* ============================================================================== */

/** Гистерезис CO2: оживление при co2 < порог - HYST (смерть при co2 >= порог). */
#ifndef BIRDY_CO2_HYST_PPM
#define BIRDY_CO2_HYST_PPM 100U
#endif
/** Подтверждение «умерла» (CO2 >= порог) — дольше, не умирать на одиночном выбросе. */
#ifndef BIRDY_STATUS_DEAD_TICKS
#define BIRDY_STATUS_DEAD_TICKS 5U
#endif
/** Подтверждение «ожила» (CO2 < порог - HYST) — быстрее. */
#ifndef BIRDY_STATUS_REVIVE_TICKS
#define BIRDY_STATUS_REVIVE_TICKS 2U
#endif

#define BIRDY_CHIRP_FAMILY_MAX 5U
#define BIRDY_CHIRP_VARIANT_MAX 5U

struct BirdySettings {
  uint8_t motionEnabled;
  uint16_t transitionSec;
  int8_t angleAlive;
  int8_t angleDead;
  uint16_t co2Bad;
  uint8_t soundEnabled;
  uint8_t chirpUp;
  uint8_t chirpDown;
  uint8_t chirpUpVar;
  uint8_t chirpDownVar;
};

inline BirdySettings birdyDefaultSettings() {
  BirdySettings s;
  s.motionEnabled  = 1;
  s.transitionSec  = BIRDY_TRANSITION_SEC;
  s.angleAlive     = BIRDY_ANGLE_ALIVE;
  s.angleDead      = BIRDY_ANGLE_DEAD;
  s.co2Bad         = BIRDY_CO2_BAD;
  s.soundEnabled   = BIRDY_SOUND_ENABLED;
  s.chirpUp        = BIRDY_CHIRP_UP;
  s.chirpDown      = BIRDY_CHIRP_DOWN;
  s.chirpUpVar     = BIRDY_CHIRP_UP_VAR;
  s.chirpDownVar   = BIRDY_CHIRP_DOWN_VAR;
  return s;
}
