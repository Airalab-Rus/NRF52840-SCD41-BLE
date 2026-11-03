#include "BirdyCtl.h"
#include "BirdConfig.h"
#include <bluefruit.h>
#include <strings.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define BIRDY_CFG_PATH "/birdy.cfg"
#define BIRDY_CFG_VER  3 /* 3: делитель 15к/15к -> batcal 100% */

/* Защита SPC: калибровать «по текущему» можно только в чистом воздухе. */
#ifndef BIRDY_SPC_MAX_PPM
#define BIRDY_SPC_MAX_PPM 600U
#endif

/* Максимум одновременных подключений (должно совпадать с BTHOME_PRPH_CONN). */
#ifndef BIRDY_MAX_CONN
#define BIRDY_MAX_CONN 2
#endif

/* ---------------------------------- BLE ---------------------------------- */
BLEService        ctlSvc(0xFFF0);
BLECharacteristic cmdChr(0xFFF1);   /* бинарные команды */
BLECharacteristic statChr(0xFFF2);  /* бинарный ответ */
BLECharacteristic infoChr(0xFFF3);  /* бинарное чтение конфига */
BLECharacteristic txtCmdChr(0xFFF4);  /* ТЕКСТОВЫЕ команды: thr=1200 alive=0 */
BLECharacteristic txtStatChr(0xFFF5); /* ТЕКСТОВЫЙ ответ */

/* --------------------------------- Состояние --------------------------------- */
static BirdyRuntime _run;
static BirdyHooks   _hooks;
static uint16_t _lastCo2 = 400;
static uint16_t _lastMv = 0; /* последнее напряжение АКБ, мВ */
static bool     _sensorOk = false;
static bool     _dead = false;
static volatile bool _forceMeasure = false;
static uint32_t _winUntilMs = 0;
static bool     _fsOk = false;

BirdyRuntime &birdyRun(void) { return _run; }

/* --------------------------------- Дефолты --------------------------------- */
static BirdyRuntime defaults(void) {
  BirdyRuntime r;
  r.co2Bad         = BIRDY_CO2_BAD;
  r.hystPpm        = BIRDY_CO2_HYST_PPM;
  r.angleAlive     = BIRDY_ANGLE_ALIVE;
  r.angleDead      = BIRDY_ANGLE_DEAD;
  r.transitionSec  = BIRDY_TRANSITION_SEC;
  r.soundEnabled   = BIRDY_SOUND_ENABLED;
  r.chirpUp        = BIRDY_CHIRP_UP;
  r.chirpDown      = BIRDY_CHIRP_DOWN;
  r.chirpUpVar     = BIRDY_CHIRP_UP_VAR;
  r.chirpDownVar   = BIRDY_CHIRP_DOWN_VAR;
  r.batCalPct      = 100; /* 100 %: делитель 15к/15к читается без поправки */
  r.toffTenths     = 0;   /* смещение температуры: 0 = как с завода */
  r.cfgVer         = BIRDY_CFG_VER;
  return r;
}

/* ----------------------------------- NVS ----------------------------------- */
static void clampRun(void) {
  if (_run.co2Bad < 300)  _run.co2Bad = 300;
  if (_run.co2Bad > 3000) _run.co2Bad = 3000;
  if (_run.hystPpm > 500) _run.hystPpm = 500;
  if (_run.angleAlive < -100) _run.angleAlive = -100;
  if (_run.angleAlive > 100)  _run.angleAlive = 100;
  if (_run.angleDead < -100)  _run.angleDead = -100;
  if (_run.angleDead > 100)   _run.angleDead = 100;
  if (_run.transitionSec > 30) _run.transitionSec = 30;
  _run.soundEnabled = _run.soundEnabled ? 1 : 0;
  if (_run.batCalPct < 50)  _run.batCalPct = 50;
  if (_run.batCalPct > 400) _run.batCalPct = 400; /* 10 МОм делитель сильно просаживается */
  if (_run.toffTenths < -50) _run.toffTenths = -50;
  if (_run.toffTenths > 50)  _run.toffTenths = 50;
}

static void loadCfg(void) {
  _run = defaults();
  if (!_fsOk) return;

  File f(InternalFS);
  if (!f.open(BIRDY_CFG_PATH, FILE_O_READ)) {
    Serial.println("[ctl] cfg: файла нет, дефолты");
    return;
  }
  char buf[256];
  int n = f.read(buf, sizeof(buf) - 1);
  f.close();
  if (n <= 0) return;
  buf[n] = 0;

  char *saveptr = nullptr;
  for (char *ln = strtok_r(buf, "\r\n", &saveptr); ln; ln = strtok_r(nullptr, "\r\n", &saveptr)) {
    char *eq = strchr(ln, '=');
    if (eq == nullptr) continue;
    *eq = 0;
    long v = atol(eq + 1);
    if (!strcmp(ln, "co2bad"))       _run.co2Bad = (uint16_t)v;
    else if (!strcmp(ln, "hyst"))    _run.hystPpm = (uint16_t)v;
    else if (!strcmp(ln, "alive"))   _run.angleAlive = (int8_t)v;
    else if (!strcmp(ln, "dead"))    _run.angleDead = (int8_t)v;
    else if (!strcmp(ln, "sec"))     _run.transitionSec = (uint8_t)v;
    else if (!strcmp(ln, "snd"))     _run.soundEnabled = (uint8_t)v;
    else if (!strcmp(ln, "cup"))     _run.chirpUp = (uint8_t)v;
    else if (!strcmp(ln, "cdown"))   _run.chirpDown = (uint8_t)v;
    else if (!strcmp(ln, "cupv"))    _run.chirpUpVar = (uint8_t)v;
    else if (!strcmp(ln, "cdownv"))  _run.chirpDownVar = (uint8_t)v;
    else if (!strcmp(ln, "batcal")) _run.batCalPct = (uint8_t)v;
    else if (!strcmp(ln, "toff"))   _run.toffTenths = (int16_t)v;
    else if (!strcmp(ln, "ver"))    _run.cfgVer = (uint16_t)v;
  }
  clampRun();

  /* Миграция: старый batcal (от прежнего делителя) больше не нужен. */
  if (_run.cfgVer < 3) {
    _run.batCalPct = 100;
    _run.cfgVer = 3;
    Serial.println("[ctl] миграция cfg: batcal -> 100%");
    birdyCtlSave();
  }

  Serial.printf("[ctl] cfg загружен: thr=%u hyst=%u alive=%d dead=%d sec=%u snd=%u\n",
                _run.co2Bad, _run.hystPpm, (int)_run.angleAlive, (int)_run.angleDead,
                _run.transitionSec, _run.soundEnabled);
}

void birdyCtlSave(void) {
  if (!_fsOk) {
    Serial.println("[ctl] save: FS недоступна");
    return;
  }
  char buf[256];
  int n = snprintf(buf, sizeof(buf),
                   "ver=%d\nco2bad=%u\nhyst=%u\nalive=%d\ndead=%d\nsec=%u\nsnd=%u\n"
                   "cup=%u\ncdown=%u\ncupv=%u\ncdownv=%u\nbatcal=%u\ntoff=%d\n",
                   BIRDY_CFG_VER, _run.co2Bad, _run.hystPpm, (int)_run.angleAlive,
                   (int)_run.angleDead, _run.transitionSec, _run.soundEnabled,
                   _run.chirpUp, _run.chirpDown, _run.chirpUpVar, _run.chirpDownVar,
                   _run.batCalPct, (int)_run.toffTenths);

  File f(InternalFS);
  if (!f.open(BIRDY_CFG_PATH, FILE_O_WRITE)) {
    Serial.println("[ctl] save: не открыть файл");
    return;
  }
  f.write(buf, n);
  f.close();
  Serial.println("[ctl] cfg сохранён в NVS");
}

void birdyCtlReset(void) {
  _run = defaults();
  clampRun();
  if (_hooks.applySettings) _hooks.applySettings();
  birdyCtlSave();
  Serial.println("[ctl] настройки сброшены к дефолтам");
}

/* --------------------------------- BLE: ответы --------------------------------- */
static void notifyStatus(uint8_t code) {
  uint8_t pkt[6];
  pkt[0] = code;
  pkt[1] = (uint8_t)(_lastCo2 & 0xFF);
  pkt[2] = (uint8_t)(_lastCo2 >> 8);
  pkt[3] = (uint8_t)((_dead ? 1 : 0) | (_run.soundEnabled ? 2 : 0) | (_sensorOk ? 4 : 0));
  pkt[4] = (uint8_t)(_run.co2Bad & 0xFF);
  pkt[5] = (uint8_t)(_run.co2Bad >> 8);

  /* Два подключения = два получателя: шлём в каждый по отдельности. */
  uint16_t hdls[BLE_MAX_CONNECTION];
  uint8_t n = Bluefruit.getConnectedHandles(hdls, BLE_MAX_CONNECTION);
  for (uint8_t i = 0; i < n; i++) {
    statChr.notify(hdls[i], pkt, sizeof(pkt));
  }
}

static void updateInfo(void) {
  uint8_t b[16];
  b[0]  = BIRDY_CFG_VER;
  b[1]  = (uint8_t)(_run.co2Bad & 0xFF);
  b[2]  = (uint8_t)(_run.co2Bad >> 8);
  b[3]  = (uint8_t)_run.angleAlive;
  b[4]  = (uint8_t)_run.angleDead;
  b[5]  = _run.transitionSec;
  b[6]  = _run.soundEnabled;
  b[7]  = _dead ? 1 : 0;
  b[8]  = (uint8_t)(_lastCo2 & 0xFF);
  b[9]  = (uint8_t)(_lastCo2 >> 8);
  b[10] = (uint8_t)(_run.hystPpm & 0xFF);
  b[11] = (uint8_t)(_run.hystPpm >> 8);
  b[12] = (uint8_t)(_lastMv & 0xFF);
  b[13] = (uint8_t)(_lastMv >> 8);
  b[14] = (uint8_t)(_run.toffTenths & 0xFF);
  b[15] = (uint8_t)((_run.toffTenths >> 8) & 0xFF);
  infoChr.write(b, sizeof(b));
}

/* -------------------------------- BLE: команды -------------------------------- */
static void handleText(char *buf); /* парсер текстовых команд — ниже */

static void onCmdWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
  (void)conn_hdl;
  (void)chr;
  if (len < 1) return;

  /* Если пришли ASCII-байты (≥0x20) — это текстовая команда, хотя бы в FFF1.
   * Удобно для приложений, которые умеют писать только в одну (бинарную) характеристику:
   *   "alive=100" / "goto=30" / "batcal=115" / "get".
   * Бинарные команды — все 0x01…0x0D, то есть < 0x20, конфликта нет. */
  if (data[0] >= 0x20) {
    char tbuf[48];
    uint16_t n = (len < sizeof(tbuf) - 1) ? len : (uint16_t)(sizeof(tbuf) - 1);
    memcpy(tbuf, data, n);
    tbuf[n] = 0;
    handleText(tbuf);
    return;
  }

  const uint8_t cmd = data[0];
  uint8_t code = BST_OK;

  switch (cmd) {
    /* Калибровка CO2 убрана: датчик калиброван на заводе (AFC делает сам). */
    case BCMD_SET_TOFF:
      if (len < 2) { code = BST_BAD_ARG; break; }
      _run.toffTenths = (int16_t)(int8_t)data[1];
      clampRun();
      if (_hooks.applySettings) _hooks.applySettings();
      birdyCtlSave();
      break;

    case BCMD_SET_THRESH: {
      if (len < 3) { code = BST_BAD_ARG; break; }
      uint16_t v = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
      if (v < 300 || v > 3000) { code = BST_BAD_ARG; break; }
      _run.co2Bad = v;
      if (_hooks.applySettings) _hooks.applySettings();
      birdyCtlSave();
      break;
    }

    case BCMD_SET_ALIVE:
      if (len < 2) { code = BST_BAD_ARG; break; }
      _run.angleAlive = (int8_t)data[1];
      clampRun();
      if (_hooks.applySettings) _hooks.applySettings();
      birdyCtlSave();
      break;

    case BCMD_SET_DEAD:
      if (len < 2) { code = BST_BAD_ARG; break; }
      _run.angleDead = (int8_t)data[1];
      clampRun();
      if (_hooks.applySettings) _hooks.applySettings();
      birdyCtlSave();
      break;

    case BCMD_SET_TRANS:
      if (len < 2 || data[1] > 30) { code = BST_BAD_ARG; break; }
      _run.transitionSec = data[1];
      if (_hooks.applySettings) _hooks.applySettings();
      birdyCtlSave();
      break;

    case BCMD_FORCE_MEAS:
      _forceMeasure = true;
      break;

    case BCMD_RESET:
      birdyCtlReset();
      break;

    case BCMD_SOUND:
      if (len < 2) { code = BST_BAD_ARG; break; }
      _run.soundEnabled = data[1] ? 1 : 0;
      birdyCtlSave();
      break;

    case BCMD_CHIRP_UP:
      if (_hooks.chirp) _hooks.chirp(true);
      break;

    case BCMD_CHIRP_DOWN:
      if (_hooks.chirp) _hooks.chirp(false);
      break;

    case BCMD_SERVO_SWEEP:
      if (_hooks.sweep) _hooks.sweep();
      break;

    case BCMD_GOTO:
      if (len < 2) { code = BST_BAD_ARG; break; }
      if (_hooks.moveTo) _hooks.moveTo((int)(int8_t)data[1]);
      break;

    default:
      code = BST_UNKNOWN_CMD;
      break;
  }

  Serial.printf("[ctl] cmd 0x%02X (len=%u) -> code %u | thr=%u alive=%d dead=%d sec=%u snd=%u\n",
                cmd, (unsigned)len, code, _run.co2Bad, (int)_run.angleAlive,
                (int)_run.angleDead, _run.transitionSec, _run.soundEnabled);
  updateInfo(); /* ФИКС: раньше FFF3 не обновлялся после записи настроек */
  notifyStatus(code);
}

/* ---------------------- Текстовые команды 0xFFF4 -> ответ 0xFFF5 ----------------------
 * С телефона (nRF Connect: Write -> Text UTF-8):
 *   thr=1200    порог «смерти», ppm          alive=0   угол «жива», -100..100
 *   hyst=100    гистерезис, ppm              dead=100  угол «мёртва»
 *   sec=8       время хода серво, сек        snd=1     звук вкл/выкл
 *   cal | cal=400   SPC-калибровка          meas      немедленный замер
 *   sweep       плавный тест серво         up/down   тест трели
 *   reset       к дефолтам                 get       просто прочитать
 * Несколько сразу: "thr=1200 alive=0". Ответ — текстом в 0xFFF5:
 *   "ok thr=1200 hyst=100 alive=0 dead=100 sec=8 snd=1 co2=1250 deadstate=1"
 */
static void fmtStatusText(char *buf, size_t n) {
  snprintf(buf, n, "thr=%u hyst=%u alive=%d dead=%d sec=%u snd=%u co2=%u deadstate=%u bat=%umv toff=%d",
           _run.co2Bad, _run.hystPpm, (int)_run.angleAlive, (int)_run.angleDead,
           _run.transitionSec, _run.soundEnabled, _lastCo2, _dead ? 1 : 0, _lastMv,
           (int)_run.toffTenths);
}

static void onTxtWrite(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
  (void)conn_hdl;
  (void)chr;
  if (len == 0) return;

  char buf[48];
  if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
  memcpy(buf, data, len);
  buf[len] = 0;
  handleText(buf);
}

static void handleText(char *buf) {
  bool changed = false;
  bool ok = true;

  char *saveptr = nullptr;
  for (char *tok = strtok_r(buf, " ,;", &saveptr); tok; tok = strtok_r(nullptr, " ,;", &saveptr)) {
    char *eq = strchr(tok, '=');
    long v = 0;
    if (eq) {
      *eq = 0;
      v = atol(eq + 1);
    }

    if (!strcasecmp(tok, "thr") && eq) { _run.co2Bad = (uint16_t)v; changed = true; }
    else if (!strcasecmp(tok, "hyst") && eq) { _run.hystPpm = (uint16_t)v; changed = true; }
    else if (!strcasecmp(tok, "alive") && eq) { _run.angleAlive = (int8_t)v; changed = true; }
    else if (!strcasecmp(tok, "dead") && eq) { _run.angleDead = (int8_t)v; changed = true; }
    else if (!strcasecmp(tok, "sec") && eq) { _run.transitionSec = (uint8_t)v; changed = true; }
    else if (!strcasecmp(tok, "snd") && eq) { _run.soundEnabled = v ? 1 : 0; changed = true; }
    else if (!strcasecmp(tok, "get")) { /* только прочитать */ }
    else if (!strcasecmp(tok, "bat")) { /* только прочитать АКБ */ }
    else if (!strcasecmp(tok, "goto") && eq) { if (_hooks.moveTo) _hooks.moveTo((int)v); }
    else if (!strcasecmp(tok, "batcal") && eq) { _run.batCalPct = (uint8_t)v; changed = true; }
    else if (!strcasecmp(tok, "meas")) { _forceMeasure = true; }
    else if (!strcasecmp(tok, "sweep")) { if (_hooks.sweep) _hooks.sweep(); }
    else if (!strcasecmp(tok, "up")) { if (_hooks.chirp) _hooks.chirp(true); }
    else if (!strcasecmp(tok, "down")) { if (_hooks.chirp) _hooks.chirp(false); }
    else if (!strcasecmp(tok, "reset")) { birdyCtlReset(); changed = true; }
    else if (!strcasecmp(tok, "toff") && eq) { _run.toffTenths = (int16_t)v; changed = true; }
    else {
      ok = false;
    }
  }

  if (changed) {
    clampRun();
    if (_hooks.applySettings) _hooks.applySettings();
    birdyCtlSave();
    updateInfo(); /* FFF3 сразу показывает новые значения */
  }

  char st[96];
  char answer[112];
  fmtStatusText(st, sizeof(st));
  snprintf(answer, sizeof(answer), "%s %s", ok ? "ok" : "err", st);
  birdyCtlText(answer);
  notifyStatus(ok ? BST_OK : BST_UNKNOWN_CMD);
}

/* ------------------------------ Колбэки подключения ------------------------------ */
static uint8_t _connCount = 0;

static void onPeerConnect(uint16_t conn_hdl) {
  _connCount = Bluefruit.Periph.connected();
  Serial.printf("[ctl] подключение hdl=%u, всего %u/%u\n",
                (unsigned)conn_hdl, _connCount, (unsigned)BIRDY_MAX_CONN);
  /* Ядро гасит рекламу при коннекте. Пока есть свободный слот — включаем снова,
   * чтобы смогло подключиться второе устройство (телефон + планшет). */
  if (_connCount < BIRDY_MAX_CONN) {
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.start(0);
    _winUntilMs = millis() + 30000UL;
    Serial.println("[ctl] реклама оставлена — ждём второе подключение");
  }
}

static void onPeerDisconnect(uint16_t conn_hdl, uint8_t reason) {
  _connCount = Bluefruit.Periph.connected();
  Serial.printf("[ctl] отключение hdl=%u (reason 0x%02X), осталось %u\n",
                (unsigned)conn_hdl, (unsigned)reason, _connCount);
  if (_connCount == 0 && _winUntilMs && (int32_t)(millis() - _winUntilMs) >= 0) {
    Bluefruit.Advertising.stop();
    _winUntilMs = 0;
    Serial.println("[ctl] все отключены, реклама остановлена");
  }
}

/* ---------------------------------- Публичное ---------------------------------- */
void birdyCtlBegin(const BirdyHooks &hooks) {
  _hooks = hooks;

  Bluefruit.Periph.setConnectCallback(onPeerConnect);
  Bluefruit.Periph.setDisconnectCallback(onPeerDisconnect);

  /* NVS: InternalFS (Adafruit LittleFS) — регион 0xED000.., бутлоадер цел. */
  _fsOk = InternalFS.begin();
  Serial.printf("[ctl] FS: %s\n", _fsOk ? "OK" : "FAIL");
  loadCfg();
  if (_hooks.applySettings) _hooks.applySettings();

  /* GATT-сервис «Birdy Control». */
  ctlSvc.begin();

  cmdChr.setProperties(CHR_PROPS_WRITE);
  cmdChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  cmdChr.setWriteCallback(onCmdWrite);
  cmdChr.begin();

  statChr.setProperties(CHR_PROPS_NOTIFY);
  statChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  statChr.begin();

  infoChr.setProperties(CHR_PROPS_READ);
  infoChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  infoChr.begin();

  /* Текстовые команды/ответы — удобно с телефона (пишем как текст, не hex). */
  txtCmdChr.setProperties(CHR_PROPS_WRITE);
  txtCmdChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  txtCmdChr.setWriteCallback(onTxtWrite);
  txtCmdChr.begin();

  txtStatChr.setProperties(CHR_PROPS_NOTIFY);
  txtStatChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  txtStatChr.begin();

  updateInfo();

  Serial.printf("[ctl] сервис 0xFFF0 готов (cmd FFF1 / status FFF2 / info FFF3), порог %u ppm\n",
                _run.co2Bad);
}

void birdyCtlReport(uint16_t co2ppm, bool sensorOk, bool dead, uint16_t vbatMv) {
  _lastCo2 = co2ppm;
  _sensorOk = sensorOk;
  _dead = dead;
  if (vbatMv) _lastMv = vbatMv;
  updateInfo();
}

/* Текстовый ответ во все подключения (для приложений вроде nRF Connect). */
void birdyCtlText(const char *msg) {
  const size_t len = strlen(msg);
  uint16_t hdls[BLE_MAX_CONNECTION];
  uint8_t n = Bluefruit.getConnectedHandles(hdls, BLE_MAX_CONNECTION);
  for (uint8_t i = 0; i < n; i++) txtStatChr.notify(hdls[i], (const uint8_t *)msg, (uint16_t)len);
  Serial.printf("[ctl] text: %s\n", msg);
}

bool birdyCtlTakeForceMeasure(void) {
  if (!_forceMeasure) return false;
  _forceMeasure = false;
  return true;
}

void birdyCtlReportCode(uint8_t code) {
  notifyStatus(code);
}

void birdyCtlOpenWindow(uint16_t seconds) {
  _winUntilMs = millis() + (uint32_t)seconds * 1000UL;

  /* Connectable: без этого телефон физически не подключится (BTHome-реклама
   * по умолчанию non-connectable). Payload BTHome сохраняется. */
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
  if (!Bluefruit.Advertising.isRunning()) {
    Bluefruit.Advertising.setIntervalMS(200, 500);
    Bluefruit.Advertising.start(0);
  }
  Serial.printf("[ctl] окно подключения: %u с\n", (unsigned)seconds);
}

void birdyCtlTick(void) {
  if (_winUntilMs == 0) return;
  if ((int32_t)(millis() - _winUntilMs) < 0) return; /* окно ещё открыто */

  /* Время вышло: если никто не подключён — глушим рекламу (энергия). */
  if (Bluefruit.Periph.connected() == 0) {
    Bluefruit.Advertising.stop();
    Serial.println("[ctl] окно закрыто, реклама остановлена");
  }
  _winUntilMs = 0;
}
