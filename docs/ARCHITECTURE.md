# Архитектура

## Блок-схема

```
┌──────────────────────────────────────────────────────────┐
│ nRF52840 SuperMini (Nice!Nano)                            │
│                                                          │
│  ┌──────────────┐   I²C (P0.17/P0.20)   ┌──────────┐     │
│  │ Arduino core │ ─────────────────────► │  SCD41   │     │
│  │ (Adafruit)   │ ◄───────────────────── │ (CO₂ T RH)│    │
│  └──────┬───────┘   readMeasurement()    └──────────┘     │
│         │                                                 │
│         │  bthome.sendPacket(500)                         │
│  ┌──────▼──────────┐   BLE 5 (S140 v6)   ┌──────────┐     │
│  │ BTHome v2       │ ───────────────────► │ HA (BLE) │     │
│  │ (Bluefruit)     │   31-byte ADV       └──────────┘     │
│  └─────────────────┘                                      │
└──────────────────────────────────────────────────────────┘
```

## Стек

| Слой | Технология | Версия |
|---|---|---|
| МК | nRF52840 (64 MHz, 512 KB Flash, 128+16 KB RAM) | — |
| RTOS / core | Arduino (Adafruit nRF52840 core) | из PlatformIO `nordicnrf52` |
| BLE | Nordic SoftDevice **S140 v6.1.1** | на чипе |
| BLE API | Adafruit Bluefruit (в ядре) | — |
| I²C | `Wire` (Arduino) на P0.17/P0.20 | — |
| Датчик | **SparkFun SCD4x Arduino Library** | ^1.1.2 |
| Протокол отчёта | **BTHome v2** (портировка Chreece/BTHomeV2) | MIT |
| Сборка | PlatformIO Core, `platform = nordicnrf52`, `board = nicenano` | 6.1.19 |

## Память

```
0x000000 ─ UF2 bootloader (Adafruit)
0x005000 ─ SoftDevice S140 v6
0x026000 ─ app (наш код): 135328 байт (16.6 %)
RAM      ─ 15636 байт (6.3 % из 248832)
```

## Цикл работы (main loop)

```
┌──────────────────────────────────────────────────────────┐
│ boot                                                     │
│   Serial.begin(115200)                                   │
│   bthome.begin("CO2-AirNode", no-encrypt)                │
│   Wire.setPins(17,20); Wire.begin()                      │
│   mySensor.begin() → setSensorAltitude(160)              │
│   doMeasure() + sendReport()   ← первый замер сразу      │
└──────────────────┬───────────────────────────────────────┘
                   ▼
        ┌────────────────────┐
        │ loop():            │
        │ if (Δ ≥ 60 s)      │
        │   doMeasure()      │◄──────────────────┐
        │   sendReport()     │                   │
        │ delay(200)         │───────────────────┘
        └────────────────────┘
```

`doMeasure()`:
1. `mySensor.startLowPowerPeriodicMeasurement()` — датчик сам меряет раз в 1.5 с
2. `delay(4500)` — дать датчику начать первый цикл
3. Цикл `while (readMeasurement() == false) delay(250)` до готовности (≤ 120 с)
4. **Читаем глобалы библиотеки** `getCO2()/getTemperature()/getHumidity()`
   — БЕЗ повторного `readMeasurement()`
5. `mySensor.stopPeriodicMeasurement(0)` + `Wire.end()` — I²C отключён

`sendReport()`:
1. `bthome.resetMeasurement()`
2. `addMeasurement(ID_PACKET, packet_id++)`
3. `addMeasurement(ID_TEMPERATURE_PRECISE, temp)` — 0.01 °C
4. `addMeasurement(ID_HUMIDITY_PRECISE, hum)` — 0.01 %
5. `addMeasurement(ID_CO2, co2)` — ppm
6. `addMeasurement(ID_BATTERY, 100)` — условно
7. `addMeasurement_state(STATE_BATTERY_LOW, 0)`
8. `sendPacket(500)` — advertise 500 мс, radio off

## Почему нет повторного `readMeasurement()` (история бага v0.1 → v0.2)

**Симптом (v0.1):** в HA статично `400 ppm / 0.0 °C / 0.0 %`, хотя в логе
`SCD41 begin: OK` и `startLowPower: OK`.

**Причина:** в low-power-режиме SCD4x хранит один результат в внутреннем
буфере. `readMeasurement()` читает и **опустошает** буфер. До следующего
результата — ~1.5 с (период внутри датчика). Второй `readMeasurement()`
в нашем коде возвращал `false` (буфер пуст) → флаг `got=false` → в отчёт
уходили значения по умолчанию (400/0.0/0.0).

**Исправление (v0.2):** читать глобалы библиотеки (`getCO2()` и т. д.)
**непосредственно после успешного** `readMeasurement()`, без повторного
вызова. Это 1-в-1 совпадает с работающим эталоном
(oxyphil-x/CO2-BTHome-sensor).

**Диагностический лог**, который это подтвердил:

```
[meas] startLowPower: OK
[meas] read: FAIL (readyIn=505ms)     ← 2-й read: буфер пуст
[meas] measure: FAIL co2=400 temp=0.0C rh=0.0%   ← в отчёт ушли default
```

**Правило для будущих изменений:** в `doMeasure()` — один цикл ожидания
`readMeasurement()` + один блок чтения глобалов. Никаких дополнительных
вызовов `readMeasurement()` внутри функции.

## Почему BTHome, а не GATT

| Критерий | BTHome (наш) | GATT-сервис |
|---|---|---|
| HA-интеграция | native, без кода HA | нужен HA-интегратор |
| Расход энергии датчика | минимальный (только advertise) | нужно держать соединение |
| Сложность кода | ~500 строк (BTHome.cpp) | ~1000 строк (Zephyr BLE) |
| Совместимость с другими | любой BTHome-приёмник | только HA |
| OTA / команды | нет (read-only) | есть |

Для **read-only** датчика качества воздуха BTHome — оптимальный выбор.

## Зависимости и лицензии

- `SparkFun SCD4x Arduino Library` — BSD-3 (автор SparkFun)
- `BTHome.cpp/.h` — портировка `Chreece/BTHomeV2-ESP32-example` (MIT)
- `variants/nicenano/` — Arduino-вариант (LGPL-2.1+)
- Основной код (`main.cpp`) — **MIT** (Airalab-Rus)
