#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "BirdConfig.h"

/**
 * Пьезо на nRF52840 — порт BirdPiezo из Birdie (там ESP32/LEDC).
 *
 * ВАЖНО: tone() из Arduino-ядра НЕ умеет менять скважность (громкость) и
 * жёстко привязан к NRF_PWM2. Поэтому работаем напрямую с HardwarePWM
 * (аналог ledcAttach/ledcWrite):
 *    frequency -> COUNTERTOP = 16 MHz / freq
 *    volume    -> duty = COUNTERTOP * dutyPct / 100
 * Так получается настоящая огибающая Birdie: нарастание при оживлении,
 * угасание при смерти + питч-рамп по нотам.
 *
 * Модуль PWM выбирается свободный (не тот, что занял Servo).
 */
class BirdPiezo {
public:
  struct Note {
    uint16_t freqHz;  /* 0 = пауза */
    uint16_t ms;
    uint8_t dutyPct;  /* громкость 1..100; 0 = тишина */
  };

  void begin();
  void tick();
  /** family 0..5 — вид птицы; variant 0..5 — вариант трели (детюн + темп). */
  void playChirp(bool goingUp, uint8_t family, uint8_t variant = 0);
  /** Громкость 0..100 % (масштабирует скважность PWM). */
  void setVolume(uint8_t pct);
  void stop();
  bool isPlaying() const { return _playing; }

private:
  uint8_t _volPct = 70; /* громкость, % (из настроек) */
  bool _ready;
  bool _playing;
  bool _goingUp;
  uint8_t _variant;
  size_t _noteIdx;
  uint32_t _noteStartMs;
  const Note *_seq;
  size_t _seqCount;

  Note shapeNote(const Note &note) const;
  void startNote(const Note &note);
  void beginBank(bool goingUp, uint8_t family);
  /** Аппаратный PWM: частота + скважность (громкость). */
  void toneOut(uint16_t freqHz, uint8_t dutyPct);
};
