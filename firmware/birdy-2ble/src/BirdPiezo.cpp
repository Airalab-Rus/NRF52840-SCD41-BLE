#include "BirdPiezo.h"
#include "HardwarePWM.h" /* ядро nRF52: HwPWMx[] + HardwarePWM */

/* Огибающая трели — как в Birdie. */
#ifndef AIRA_CHIRP_PITCH_RAMP
#define AIRA_CHIRP_PITCH_RAMP 0.30f
#endif
#ifndef AIRA_CHIRP_VOL_FLOOR
#define AIRA_CHIRP_VOL_FLOOR 0.25f
#endif
/* Минимальная длина ноты: у nRF52 перезапуск PWM добавляет пару мс накладных. */
#ifndef BIRDY_NOTE_MIN_MS
#define BIRDY_NOTE_MIN_MS 6
#endif

/* Выбранный модуль HardwarePWM под пьезо (Servo обычно занимает HwPWM0). */
static HardwarePWM *_pwm = nullptr;
static const uint32_t PIEZO_TOKEN = 0x70657a6f; /* 'p' 'e' 'z' 'o' */

static const BirdPiezo::Note kUpCanary[] = {
    {2880, 7, 20}, {3120, 7, 26}, {2900, 7, 32}, {3140, 7, 38}, {2920, 7, 44}, {3160, 7, 50},
    {2960, 7, 56}, {3200, 7, 62}, {3000, 8, 68}, {3240, 8, 74}, {3040, 8, 80}, {3280, 9, 86}, {3100, 10, 92},
};

static const BirdPiezo::Note kUpGoldfinch[] = {
    {3520, 5, 24}, {3780, 5, 32}, {3560, 5, 40}, {3820, 5, 48}, {3600, 5, 56}, {3860, 5, 64},
    {3640, 5, 72}, {3900, 6, 80}, {3680, 6, 88}, {3940, 7, 95},
};

static const BirdPiezo::Note kUpRobin[] = {
    {3180, 20, 28}, {0, 14, 0}, {3360, 18, 42}, {3520, 16, 54}, {0, 12, 0},
    {3440, 18, 66}, {3620, 20, 78}, {3780, 22, 90},
};

static const BirdPiezo::Note kUpWren[] = {
    {3020, 5, 26}, {3220, 5, 32}, {3060, 5, 38}, {3260, 5, 44}, {0, 16, 0},
    {3100, 5, 50}, {3300, 5, 58}, {3140, 5, 66}, {3340, 6, 74}, {0, 14, 0},
    {3180, 6, 82}, {3380, 7, 90}, {3220, 8, 96},
};

static const BirdPiezo::Note kUpSkylark[] = {
    {2760, 6, 22}, {2840, 6, 28}, {2920, 6, 34}, {3000, 6, 40}, {3080, 6, 46}, {3160, 6, 52},
    {3240, 6, 58}, {3320, 6, 64}, {3400, 7, 70}, {3480, 7, 76}, {3560, 7, 82}, {3640, 8, 88}, {3720, 9, 94},
};

static const BirdPiezo::Note kUpNightingale[] = {
    {2680, 9, 24}, {3020, 8, 32}, {2780, 9, 40}, {3160, 8, 48}, {2860, 9, 56}, {3280, 8, 64},
    {2940, 9, 72}, {3380, 8, 80}, {3020, 10, 86}, {3460, 10, 92}, {3100, 12, 96},
};

static const BirdPiezo::Note kDownCanary[] = {
    {3260, 7, 88}, {3020, 7, 78}, {3240, 7, 68}, {3000, 7, 58}, {3220, 7, 48}, {2980, 8, 38},
    {3180, 8, 28}, {2940, 9, 18},
};

static const BirdPiezo::Note kDownGoldfinch[] = {
    {3880, 5, 90}, {3620, 5, 78}, {3840, 5, 66}, {3580, 5, 54}, {3800, 5, 42}, {3540, 6, 30}, {3720, 7, 18},
};

static const BirdPiezo::Note kDownRobin[] = {
    {3600, 22, 82}, {0, 26, 0}, {3220, 24, 58}, {0, 28, 0}, {2840, 28, 32}, {0, 30, 0}, {2460, 32, 14},
};

static const BirdPiezo::Note kDownWren[] = {
    {3340, 5, 86}, {3140, 5, 74}, {3300, 5, 62}, {3100, 5, 50}, {0, 18, 0},
    {3220, 6, 38}, {3020, 6, 26}, {0, 20, 0}, {2940, 8, 16},
};

static const BirdPiezo::Note kDownSkylark[] = {
    {3680, 6, 88}, {3600, 6, 76}, {3520, 6, 64}, {3440, 6, 52}, {3360, 6, 42}, {3280, 6, 34},
    {3200, 7, 26}, {3120, 7, 20}, {3040, 8, 14},
};

static const BirdPiezo::Note kDownNightingale[] = {
    {3420, 9, 84}, {3080, 9, 72}, {3340, 9, 60}, {2980, 9, 48}, {3220, 10, 36}, {2860, 10, 24}, {3020, 12, 14},
};

struct ChirpBank {
  const BirdPiezo::Note *notes;
  size_t count;
};

static const ChirpBank kUpProfiles[] = {
    {kUpCanary, sizeof(kUpCanary) / sizeof(kUpCanary[0])},
    {kUpGoldfinch, sizeof(kUpGoldfinch) / sizeof(kUpGoldfinch[0])},
    {kUpRobin, sizeof(kUpRobin) / sizeof(kUpRobin[0])},
    {kUpWren, sizeof(kUpWren) / sizeof(kUpWren[0])},
    {kUpSkylark, sizeof(kUpSkylark) / sizeof(kUpSkylark[0])},
    {kUpNightingale, sizeof(kUpNightingale) / sizeof(kUpNightingale[0])},
};

static const ChirpBank kDownProfiles[] = {
    {kDownCanary, sizeof(kDownCanary) / sizeof(kDownCanary[0])},
    {kDownGoldfinch, sizeof(kDownGoldfinch) / sizeof(kDownGoldfinch[0])},
    {kDownRobin, sizeof(kDownRobin) / sizeof(kDownRobin[0])},
    {kDownWren, sizeof(kDownWren) / sizeof(kDownWren[0])},
    {kDownSkylark, sizeof(kDownSkylark) / sizeof(kDownSkylark[0])},
    {kDownNightingale, sizeof(kDownNightingale) / sizeof(kDownNightingale[0])},
};

static_assert(sizeof(kUpProfiles) / sizeof(kUpProfiles[0]) == BIRDY_CHIRP_FAMILY_MAX + 1, "up families");
static_assert(sizeof(kDownProfiles) / sizeof(kDownProfiles[0]) == BIRDY_CHIRP_FAMILY_MAX + 1, "down families");

/* ---------------------------------- PWM ---------------------------------- */

void BirdPiezo::toneOut(uint16_t freqHz, uint8_t dutyPct) {
  if (!_ready || _pwm == nullptr) return;
  if (freqHz == 0 || dutyPct == 0) {
    _pwm->writePin(BIRDY_PIEZO_PIN, 0); /* скважность 0 = тишина */
    return;
  }
  /* 16 МГц / freq -> COUNTERTOP; для 2.4-4 кГц это 4000..6600 тиков (15 бит ОК). */
  uint32_t top = 16000000UL / (uint32_t)freqHz;
  if (top > 30000UL) top = 30000UL;
  if (top < 100UL) top = 100UL;
  uint32_t duty = (top * (uint32_t)dutyPct) / 100U;
  if (duty < 1) duty = 1;
  if (duty >= top) duty = top - 1;

  _pwm->setMaxValue((uint16_t)top);
  _pwm->writePin(BIRDY_PIEZO_PIN, (uint16_t)duty);
}

/* -------------------------------- Огибающая -------------------------------- */

BirdPiezo::Note BirdPiezo::shapeNote(const Note &note) const {
  Note shaped = note;
  if (shaped.freqHz > 0) {
    if (_variant != 0) {
      const float detune = 1.0f + 0.03f * (float)_variant;
      long f = (long)((float)shaped.freqHz * detune);
      if (f > 20000) f = 20000;
      shaped.freqHz = (uint16_t)f;
    }
    if (_seqCount > 1) {
      const float p = (float)_noteIdx / (float)(_seqCount - 1);
      /* Питч-рамп: вверх — растёт, вниз — падает (как в Birdie). */
      const float pitchRamp = _goingUp ? (1.0f + AIRA_CHIRP_PITCH_RAMP * p)
                                       : (1.0f - AIRA_CHIRP_PITCH_RAMP * p);
      long f = (long)((float)shaped.freqHz * pitchRamp);
      if (f < 200) f = 200;
      if (f > 20000) f = 20000;
      shaped.freqHz = (uint16_t)f;
      /* Громкость: оживление — нарастает, смерть — угасает. */
      const float volMul = _goingUp ? (AIRA_CHIRP_VOL_FLOOR + (1.0f - AIRA_CHIRP_VOL_FLOOR) * p)
                                    : (1.0f - (1.0f - AIRA_CHIRP_VOL_FLOOR) * p);
      int d = (int)((float)shaped.dutyPct * volMul);
      if (d < 1) d = 1;
      if (d > 100) d = 100;
      shaped.dutyPct = (uint8_t)d;
    }
  }
  if (_variant != 0) {
    const float tempo = 1.0f - 0.04f * (float)_variant;
    long ms = (long)((float)shaped.ms * tempo);
    if (ms < 3) ms = 3;
    shaped.ms = (uint16_t)ms;
  }
  if (shaped.ms < BIRDY_NOTE_MIN_MS) shaped.ms = BIRDY_NOTE_MIN_MS;
  return shaped;
}

/* ---------------------------------- API ---------------------------------- */

void BirdPiezo::begin() {
  _ready = false;
  _playing = false;
  _goingUp = true;
  _variant = 0;
  _noteIdx = 0;
  _noteStartMs = 0;
  _seq = nullptr;
  _seqCount = 0;
  _pwm = nullptr;

  /* Свободный модуль PWM: первым делом НЕ нулевой (его обычно берёт Servo). */
  for (int pass = 0; pass < 2 && _pwm == nullptr; pass++) {
    for (int i = 0; i < HWPWM_MODULE_NUM; i++) {
      if (pass == 0 && i == 0) continue;
      if (HwPWMx[i]->takeOwnership(PIEZO_TOKEN) && HwPWMx[i]->addPin(BIRDY_PIEZO_PIN)) {
        _pwm = HwPWMx[i];
        break;
      }
    }
  }
  if (_pwm == nullptr) {
    Serial.println("[birdy] piezo FAIL: нет свободного PWM-модуля");
    return;
  }
  _pwm->setClockDiv(PWM_PRESCALER_PRESCALER_DIV_1); /* 16 МГц -> 1 тик = 62.5 нс */
  _pwm->setMaxValue(4000);
  _pwm->writePin(BIRDY_PIEZO_PIN, 0);
  _ready = true;
  Serial.printf("[birdy] piezo begin: pin=%d (HardwarePWM)\n", (int)BIRDY_PIEZO_PIN);
}

void BirdPiezo::stop() {
  if (_ready) toneOut(0, 0);
  _playing = false;
  _noteIdx = 0;
  _seq = nullptr;
  _seqCount = 0;
}

void BirdPiezo::beginBank(bool goingUp, uint8_t family) {
  if (family > BIRDY_CHIRP_FAMILY_MAX) family = BIRDY_CHIRP_FAMILY_MAX;
  const ChirpBank &bank = goingUp ? kUpProfiles[family] : kDownProfiles[family];
  _goingUp = goingUp;
  _seq = bank.notes;
  _seqCount = bank.count;
  Serial.printf("[birdy] trill %s family %u var %u (%u нот)\n",
                goingUp ? "up" : "down", (unsigned)family, (unsigned)_variant, (unsigned)bank.count);
}

void BirdPiezo::startNote(const Note &note) {
  const Note shaped = shapeNote(note);
  if (!_ready) return;
  toneOut(shaped.freqHz, shaped.dutyPct);
}

void BirdPiezo::playChirp(bool goingUp, uint8_t family, uint8_t variant) {
  if (!_ready) return;
  if (family > BIRDY_CHIRP_FAMILY_MAX) family = BIRDY_CHIRP_FAMILY_MAX;
  if (variant > BIRDY_CHIRP_VARIANT_MAX) variant = BIRDY_CHIRP_VARIANT_MAX;
  _variant = variant;
  beginBank(goingUp, family);
  if (_seq == nullptr || _seqCount == 0) return;
  _noteIdx = 0;
  _noteStartMs = millis();
  _playing = true;
  startNote(_seq[0]);
}

void BirdPiezo::tick() {
  if (!_playing || _seq == nullptr || _seqCount == 0) return;
  if (_noteIdx >= _seqCount) { stop(); return; } /* защита от гонки/мусора */
  const uint32_t now = millis();
  if (now - _noteStartMs >= _seq[_noteIdx].ms) {
    _noteIdx++;
    if (_noteIdx >= _seqCount) {
      stop();
      Serial.println("[birdy] trill done");
      return;
    }
    _noteStartMs = now;
    startNote(_seq[_noteIdx]);
  }
}
