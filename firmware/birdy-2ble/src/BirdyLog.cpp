#include "BirdyLog.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define LOG_MAGIC 0x424C4F47UL /* 'BLOG' */

struct __attribute__((packed)) TierHead {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
};

struct Tier {
  const char *path;
  uint16_t cap;
  BirdyLogRec *buf;
  TierHead h;
  uint8_t dirty;      /* сколько записей не сброшено */
  uint8_t flushEvery; /* как часто сбрасывать */
};

/* «Страховка»: если приложение падает на старте из-за лога, после 2 сбросов
 * подряд журнал отключается, и плата грузится с пустым логом.
 * GPREGRET переживает программный сброс, но обнуляется при отключении питания. */
static bool _enabled = true;

static BirdyLogRec _raw[BIRDY_LOG_RAW_MAX];
static BirdyLogRec _hour[BIRDY_LOG_HOUR_MAX];
static BirdyLogRec _day[BIRDY_LOG_DAY_MAX];

static Tier _tiers[LOG_TIER_NUM] = {
  { "/log.raw", BIRDY_LOG_RAW_MAX,  _raw,  { LOG_MAGIC, 0, 0 }, 0, 20 },
  { "/log.h",   BIRDY_LOG_HOUR_MAX, _hour, { LOG_MAGIC, 0, 0 }, 0, 1  },
  { "/log.d",   BIRDY_LOG_DAY_MAX,  _day,  { LOG_MAGIC, 0, 0 }, 0, 1  },
};

/* ------------------------------- время ------------------------------- */

static bool     _timeValid = false;
static uint32_t _epochBase = 0;   /* unix-время на момент установки */
static uint32_t _epochMs = 0;

void logSetEpoch(uint32_t epoch) {
  _epochBase = epoch;
  _epochMs = millis();
  _timeValid = true;
  Serial.printf("[log] время: %lu\n", (unsigned long)epoch);
}

uint32_t logNow(void) {
  if (!_timeValid) return 0;
  return _epochBase + (millis() - _epochMs) / 1000UL;
}

bool logTimeValid(void) { return _timeValid; }

/* ------------------------------ помощники ------------------------------ */

static void push(Tier &t, const BirdyLogRec &r) {
  t.buf[t.h.head] = r;
  t.h.head = (uint16_t)((t.h.head + 1) % t.cap);
  if (t.h.count < t.cap) t.h.count++;
  if (t.flushEvery && ++t.dirty >= t.flushEvery) logFlush();
}

static bool getRec(const Tier &t, uint16_t idx, BirdyLogRec *out) {
  if (out == nullptr || idx >= t.h.count) return false;
  const uint16_t start = (uint16_t)((t.h.head + t.cap - t.h.count) % t.cap);
  *out = t.buf[(start + idx) % t.cap];
  return true;
}

/* ------------------------------ сохранение ------------------------------ */

static void saveTier(Tier &t) {
  File f(InternalFS);
  if (!f.open(t.path, FILE_O_WRITE)) {
    Serial.printf("[log] не открыть %s\n", t.path);
    return;
  }
  f.seek(0);
  int wr = (int)f.write((const uint8_t *)&t.h, sizeof(t.h));
  if (wr != (int)sizeof(t.h)) {
    Serial.printf("[log] %s: ошибка записи заголовка (%d)\n", t.path, wr);
    f.close();
    return;
  }
  for (uint16_t i = 0; i < t.h.count; i++) {
    BirdyLogRec r;
    if (!getRec(t, i, &r)) break;
    wr = (int)f.write((const uint8_t *)&r, sizeof(r));
    if (wr != (int)sizeof(r)) {
      Serial.printf("[log] %s: ошибка на записи %u (%d)\n", t.path, i, wr);
      break;
    }
  }
  f.close();
  t.dirty = 0;
}

void logFlush(void) {
  for (uint8_t i = 0; i < LOG_TIER_NUM; i++) {
    if (_tiers[i].dirty) saveTier(_tiers[i]);
  }
}

static void loadTier(Tier &t) {
  File f(InternalFS);
  if (!f.open(t.path, FILE_O_READ)) return;
  if (f.size() < (int)sizeof(TierHead)) { f.close(); return; }
  TierHead h{};
  f.read(&h, sizeof(h));
  if (h.magic != LOG_MAGIC || h.count > t.cap) { f.close(); return; }
  t.h = h;
  for (uint16_t i = 0; i < h.count; i++) {
    BirdyLogRec r;
    if (f.read(&r, sizeof(r)) != (int)sizeof(r)) { t.h.count = i; break; }
    t.buf[i] = r;
  }
  f.close();
  t.h.head = (uint16_t)(t.h.count % t.cap);
  Serial.printf("[log] %s: %u записей\n", t.path, t.h.count);
}

/* --------------------------------- API --------------------------------- */

void logBegin(void) {
  const uint32_t gr = NRF_POWER->GPREGRET;
  if (gr >= 2) {
    _enabled = false;
    Serial.printf("[log] SAFE MODE: журнал отключён (сбросов подряд: %lu)\n",
                  (unsigned long)gr);
    return;
  }
  NRF_POWER->GPREGRET = gr + 1;

  /* файл прошлой версии формата — удаляем, чтобы не занимал блоки */
  {
    File old(InternalFS);
    if (old.open("/birdy.log", FILE_O_READ)) {
      old.close();
      InternalFS.remove("/birdy.log");
      Serial.println("[log] старый /birdy.log удалён");
    }
  }
  for (uint8_t i = 0; i < LOG_TIER_NUM; i++) loadTier(_tiers[i]);
}

static bool rtcInit = false;
static uint32_t hourKey = 0, dayKey = 0;
static uint32_t hCo2 = 0, hHum = 0, hMv = 0; static int32_t hTemp = 0; static uint16_t hN = 0;
static uint32_t dCo2 = 0, dHum = 0, dMv = 0; static int32_t dTemp = 0; static uint16_t dN = 0;

bool logAppend(uint16_t co2, float tempC, float humPct, uint16_t mv) {
  if (!_enabled) return false;
  const uint32_t now = logNow();

  BirdyLogRec r;
  r.ts = now;
  r.co2 = co2;
  r.temp = (int16_t)(tempC * 100.0f);
  r.hum = (uint16_t)(humPct * 100.0f);
  r.mv = mv;
  push(_tiers[LOG_TIER_RAW], r);

  /* без времени агрегаты не имеют смысла */
  if (!_timeValid) return true;

  const uint32_t hk = now / 3600UL;
  const uint32_t dk = now / 86400UL;

  if (!rtcInit) { rtcInit = true; hourKey = hk; dayKey = dk; }

  if (hk != hourKey) {
    /* закрываем предыдущий час */
    if (hN > 0) {
      BirdyLogRec hr;
      hr.ts = (hourKey + 1) * 3600UL;
      hr.co2 = (uint16_t)(hCo2 / hN);
      hr.temp = (int16_t)(hTemp / (int32_t)hN);
      hr.hum = (uint16_t)(hHum / hN);
      hr.mv = (uint16_t)(hMv / hN);
      push(_tiers[LOG_TIER_HOUR], hr);
    }
    hourKey = hk; hCo2 = hHum = hMv = 0; hTemp = 0; hN = 0;
  }
  hCo2 += co2; hTemp += r.temp; hHum += r.hum; hMv += mv; hN++;

  if (dk != dayKey) {
    if (dN > 0) {
      BirdyLogRec dr;
      dr.ts = (dayKey + 1) * 86400UL;
      dr.co2 = (uint16_t)(dCo2 / dN);
      dr.temp = (int16_t)(dTemp / (int32_t)dN);
      dr.hum = (uint16_t)(dHum / dN);
      dr.mv = (uint16_t)(dMv / dN);
      push(_tiers[LOG_TIER_DAY], dr);
    }
    dayKey = dk; dCo2 = dHum = dMv = 0; dTemp = 0; dN = 0;
  }
  dCo2 += co2; dTemp += r.temp; dHum += r.hum; dMv += mv; dN++;
  return true;
}

uint16_t logCount(uint8_t tier) {
  if (!_enabled || tier >= LOG_TIER_NUM) return 0;
  return _tiers[tier].h.count;
}

bool logGet(uint8_t tier, uint16_t idx, BirdyLogRec *out) {
  if (!_enabled || tier >= LOG_TIER_NUM) return false;
  return getRec(_tiers[tier], idx, out);
}

void logMarkAlive(void) {
  /* дошли до рабочего цикла — счётчик «страховки» больше не нужен */
  NRF_POWER->GPREGRET = 0;
}

void logClear(void) {
  if (!_enabled) return;
  for (uint8_t i = 0; i < LOG_TIER_NUM; i++) {
    _tiers[i].h.head = 0;
    _tiers[i].h.count = 0;
    _tiers[i].dirty = 1;
  }
  rtcInit = false; hN = 0; dN = 0;
  logFlush();
  Serial.println("[log] очищен");
}
