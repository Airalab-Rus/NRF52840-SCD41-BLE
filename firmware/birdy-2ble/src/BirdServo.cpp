#include "BirdServo.h"
#include <Servo.h>

/** Серво (nRF52840): угол -100..100 % -> 1000..3000 мкс, плавный ход в tick(). */
static Servo birdyServo;

float BirdServo::easeInOut(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

float BirdServo::moveProgress() const {
  if (!_moving || _transitionMs == 0) {
    return _moving ? 0.0f : 1.0f;
  }
  const uint32_t elapsed = millis() - _moveStartMs;
  if (elapsed >= _transitionMs) return 1.0f;
  return (float)elapsed / (float)_transitionMs;
}

void BirdServo::begin() {
  _transitionMs = (uint32_t)birdyDefaultSettings().transitionSec * 1000U;
  _currentPercent = 0;
  _startPercent = 0;
  _targetPercent = 0;
  _moveStartMs = 0;
  _moving = false;
  birdyServo.attach(BIRDY_SERVO_PIN);
  snapTo(0);
  Serial.printf("[birdy] servo begin: pin=%d transition=%u ms\n",
                (int)BIRDY_SERVO_PIN, (unsigned)_transitionMs);
}

void BirdServo::setTransitionMs(uint32_t ms) {
  if (ms == _transitionMs) return;
  _transitionMs = ms;
  if (_moving && ms > 0) {
    const uint32_t elapsed = millis() - _moveStartMs;
    if (elapsed < ms) _moveStartMs = millis() - elapsed;
  }
}

void BirdServo::applyPercent(int percent) {
  if (percent < -100) percent = -100;
  if (percent > 100) percent = 100;
  const long micro = 1000L + ((long)(percent + 100) * 10L); /* -100..100 -> 1000..3000 */
  birdyServo.writeMicroseconds((int)micro);
}

void BirdServo::snapTo(int percent) {
  if (percent < -100) percent = -100;
  if (percent > 100) percent = 100;
  _currentPercent = percent;
  _startPercent = percent;
  _targetPercent = percent;
  _moving = false;
  applyPercent(percent);
}

void BirdServo::startMoveTo(int percent) {
  if (percent < -100) percent = -100;
  if (percent > 100) percent = 100;
  if (percent == _currentPercent && percent == _targetPercent && !_moving) return;
  if (percent == _targetPercent && _moving) return;

  _startPercent = _currentPercent;
  _targetPercent = percent;
  _moveStartMs = millis();

  if (_transitionMs == 0) {
    Serial.printf("[birdy] servo %d%% -> %d%% (instant)\n", _currentPercent, percent);
    snapTo(percent);
    return;
  }
  _moving = true;
  Serial.printf("[birdy] servo %d%% -> %d%% за %u ms (ease)\n",
                _startPercent, _targetPercent, (unsigned)_transitionMs);
}

void BirdServo::writePercent(int percent) {
  startMoveTo(percent);
}

void BirdServo::redirectMoveTo(int percent) {
  if (percent < -100) percent = -100;
  if (percent > 100) percent = 100;
  if (percent == _targetPercent && _moving) return;
  _startPercent = _currentPercent;
  _targetPercent = percent;
  _moveStartMs = millis();
  if (_transitionMs == 0) {
    snapTo(percent);
    return;
  }
  _moving = true;
  Serial.printf("[birdy] servo redirect %d%% -> %d%% за %u ms (ease)\n",
                _startPercent, _targetPercent, (unsigned)_transitionMs);
}

void BirdServo::tick() {
  if (!_moving) return;

  const uint32_t elapsed = millis() - _moveStartMs;
  if (elapsed >= _transitionMs) {
    snapTo(_targetPercent);
    return;
  }
  const float t = easeInOut((float)elapsed / (float)_transitionMs);
  const int delta = _targetPercent - _startPercent;
  const int p = _startPercent + (int)(delta * t);
  _currentPercent = p;
  applyPercent(p);
}
