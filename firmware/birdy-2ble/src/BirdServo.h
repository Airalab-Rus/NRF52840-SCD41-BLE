#pragma once

#include <Arduino.h>
#include "BirdConfig.h"

/**
 * Серво на nRF52840 (Arduino core, библиотека Servo).
 * Порт BirdServo из Birdie (ESP32) без LEDC:
 *   угол -100..100 % -> writeMicroseconds(1000..3000),
 *   плавный ход с easeInOut в tick().
 *
 * Направление «вверх/вниз» (angleAlive/angleDead) задаётся в BirdConfig.h.
 */
class BirdServo {
public:
  void begin();
  void setTransitionMs(uint32_t ms);
  void writePercent(int percent);
  /** Смена цели на ходу: от текущего положения, без ожидания старого хода. */
  void redirectMoveTo(int percent);
  void tick();

  int currentPercent() const { return _currentPercent; }
  int targetPercent() const { return _targetPercent; }
  bool isMoving() const { return _moving; }
  /** 0..1 прогресс текущего хода. */
  float moveProgress() const;
  /** Мгновенно поставить в позицию (для self-test свипа, без ease). */
  void snapPercent(int percent) { snapTo(percent); }

private:
  static float easeInOut(float t);
  uint32_t _transitionMs;
  int _currentPercent;
  int _startPercent;
  int _targetPercent;
  uint32_t _moveStartMs;
  bool _moving;

  void applyPercent(int percent);
  void snapTo(int percent);
  void startMoveTo(int percent);
};
