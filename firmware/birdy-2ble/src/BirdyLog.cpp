#include "BirdyLog.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define LOG_PATH      "/birdy.log"
#define LOG_HEAD_MAGIC 0x424C4F47UL /* 'BLOG' */

struct __attribute__((packed)) LogHead {
  uint32_t magic;
  uint16_t head;   /* индекс следующей записи */
  uint16_t count;  /* сколько записей валидно (<= BIRDY_LOG_MAX) */
};

static BirdyLogRec _buf[BIRDY_LOG_MAX];
static LogHead _head = { LOG_HEAD_MAGIC, 0, 0 };
static uint16_t _pending = 0;  /* сколько записей ещё не сброшено в flash */

/* ------------------------------- сохранение ------------------------------- */

void logFlush(void) {
  File f(InternalFS);
  if (!f.open(LOG_PATH, FILE_O_WRITE)) {
    Serial.println("[log] flush: не открыть файл");
    return;
  }
  f.seek(0); /* FILE_O_WRITE в Adafruit LittleFS открывает в конец — пишем с начала */
  f.write((const uint8_t *)&_head, sizeof(_head));
  /* пишем кольцо «развёрнутым»: от самой старой к самой новой */
  for (uint16_t i = 0; i < _head.count; i++) {
    BirdyLogRec r;
    if (!logGet(i, &r)) break;
    f.write((const uint8_t *)&r, sizeof(r));
  }
  f.close();
  _pending = 0;
}

/* --------------------------------- загрузка --------------------------------- */

void logBegin(void) {
  File f(InternalFS);
  if (!f.open(LOG_PATH, FILE_O_READ)) {
    Serial.println("[log] новый файл");
    return;
  }
  const uint32_t sz = f.size();
  if (sz < sizeof(LogHead)) {
    f.close();
    return;
  }
  LogHead h{};
  f.read(&h, sizeof(h));
  if (h.magic != LOG_HEAD_MAGIC || h.count > BIRDY_LOG_MAX) {
    Serial.println("[log] файл битый — начинаем заново");
    f.close();
    return;
  }
  _head = h;
  for (uint16_t i = 0; i < h.count; i++) {
    BirdyLogRec r;
    if (f.read(&r, sizeof(r)) != (int)sizeof(r)) { _head.count = i; break; }
    _buf[i] = r;
  }
  f.close();
  /* после загрузки кольцо нормализовано: head = count % MAX */
  _head.head = (uint16_t)(_head.count % BIRDY_LOG_MAX);
  Serial.printf("[log] загружено %u записей\n", _head.count);
}

/* --------------------------------- операции --------------------------------- */

bool logAppend(uint16_t co2, float tempC, float humPct, uint16_t mv) {
  BirdyLogRec r;
  r.co2 = co2;
  r.temp = (int16_t)(tempC * 100.0f);
  r.hum = (uint16_t)(humPct * 100.0f);
  r.mv = mv;
  r.up = (uint32_t)(millis() / 1000UL);

  _buf[_head.head] = r;
  _head.head = (uint16_t)((_head.head + 1) % BIRDY_LOG_MAX);
  if (_head.count < BIRDY_LOG_MAX) _head.count++;

  if (++_pending >= 30) logFlush();
  return true;
}

uint16_t logCount(void) { return _head.count; }

bool logGet(uint16_t idx, BirdyLogRec *out) {
  if (out == nullptr || idx >= _head.count) return false;
  /* 0 = самая старая запись */
  const uint16_t start = (uint16_t)((_head.head + BIRDY_LOG_MAX - _head.count) % BIRDY_LOG_MAX);
  *out = _buf[(start + idx) % BIRDY_LOG_MAX];
  return true;
}

void logClear(void) {
  _head.head = 0;
  _head.count = 0;
  _pending = 0;
  logFlush();
  Serial.println("[log] очищен");
}
