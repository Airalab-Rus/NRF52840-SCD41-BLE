# CO2-Birdy — узел мониторинга воздуха на nRF52840 + SCD41

Автономный датчик CO₂/температуры/влажности на **nRF52840 SuperMini (Nice!Nano) + SCD41 (Sensirion)**
с передачей в Home Assistant по BLE (**BTHome v2**) — и «птичка» на серво + пьезо, которая оживает
в чистом воздухе и «умирает» с нисходящей трелью, когда CO₂ превышает порог.
Настройка — с Android-приложения по BLE.

![demo](docs/demo.gif)

[![Build](https://github.com/Airalab-Rus/NRF52840-SCD41-BLE/actions/workflows/build.yml/badge.svg)](https://github.com/Airalab-Rus/NRF52840-SCD41-BLE/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: PlatformIO](https://img.shields.io/badge/PlatformIO-nordicnrf52-blue.svg)](https://platformio.org/)

## Что в репозитории

| Часть | Папка | Что это |
|---|---|---|
| **Базовая прошивка** | [`firmware/base`](firmware/base) | CO₂-узел: SCD41 → BTHome v2 по BLE. Минимум кода, проверенный стек. |
| **Birdy 2BLE** | [`firmware/birdy-2ble`](firmware/birdy-2ble) | То же + серво, пьезо (трели), батарея на ADC, GATT-сервис управления, 2 одновременных BLE-подключения, настройки в NVS. |
| **Android-приложение** | [`android/birdy-app`](android/birdy-app) | Пульт: автосканирование, ползунки порога/углов/температуры, живой предпросмотр, батарея, тесты. |
| **Документация** | [`docs/`](docs) | Установка, распиновка, прошивка, HA, архитектура, энергобюджет, troubleshooting. |
| **Утилиты** | [`scripts/elf2uf2.py`](scripts/elf2uf2.py) | `.elf` → UF2 для UF2-бутлоадера Nice!Nano. |

## Возможности

- **SCD41** (Sensirion): CO₂, температура, влажность; фотоакустический сенсор, заводская калибровка + AFC.
- **BTHome v2** — нативная интеграция Home Assistant по BLE, без шлюзов, MQTT и облака.
- **Автономность**: замер раз в минуту, реклама окнами по 30 с; питание от Li-Ion, контроль через делитель на ADC.
- **«Птичка»**: два положения серво (жива/мёртва), плавный `easeInOut`-ход, 6 семейств трелей
  (канарейка, чиж, зарянка, крапивник, жаворонок, соловей) с нарастанием и угасанием.
- **Управление по BLE**: порог CO₂, гистерезис, углы, время хода, звук, смещение температуры, тесты —
  всё сохраняется в NVS и переживает перезагрузку.
- **Два одновременных подключения** (телефон + планшет) при сохранении BTHome-рекламы.
- **Журнал в памяти платы**: 256 последних замеров (RAM + `/birdy.log` в LittleFS), выгрузка в приложение по BLE (`0xFFF6`) с графиком
- **Отказоустойчивость I²C**: восстановление шины (9 тактов SCL + STOP), re-init датчика,
  эскалация до заводского сброса при повторных сбоях.

## Железо

| Что | Пин (Arduino) | Порт | Примечание |
|---|---|---|---|
| SCD41 SDA | 17 | P0.17 | I²C |
| SCD41 SCL | 20 | P0.20 | I²C |
| Серво (сигнал) | 24 | P0.24 | 50 Гц, 1000–3000 мкс |
| Пьезо (сигнал) | 22 | P0.22 | PWM 2.4–4 кГц |
| Батарея (делитель) | 31 | P0.31 / AIN7 | R_top = R_bot = 15 кОм (SMD 153) |

Плата: **nRF52840 SuperMini / Nice!Nano** (UF2-бутлоадер + SoftDevice S140 v6), приложение грузится с `0x26000`.

Подробная распиновка и схема — [docs/WIRING.md](docs/WIRING.md).

## Быстрый старт (прошивка UF2)

1. Соберите прошивку (см. ниже) и получите `.uf2` через [`scripts/elf2uf2.py`](scripts/elf2uf2.py).
2. **Двойной RESET** платы → появляется диск `NICENANO`.
3. Скопируйте `.uf2` на диск — плата перезагрузится в приложение.
4. В Home Assistant устройство появится само (интеграция **BTHome**).

Подробно: [docs/FLASHING.md](docs/FLASHING.md).

## Сборка из исходников

Нужны Python 3.9+ и PlatformIO Core. nRF Connect SDK не требуется — используется PlatformIO + Adafruit nRF52 core.

```bash
# базовая прошивка (только BTHome)
pio run -d firmware/base

# birdy-2ble (серво, пьезо, батарея, BLE-управление)
pio run -d firmware/birdy-2ble

# готовый UF2 для бутлоадера
python scripts/elf2uf2.py firmware/birdy-2ble/.pio/build/nicenano/firmware.elf CO2-Birdy.uf2
```

Стек: `platform = nordicnrf52`, `framework = arduino` (Adafruit nRF52 core),
`sparkfun/SparkFun SCD4x Arduino Library@^1.1.2`.

> Прошивать `.elf`/`.hex` в UF2-бутлоадер нельзя — нужен именно UF2 (конвертер в `scripts/`).

## Android-приложение

Папка [`android/birdy-app`](android/birdy-app) — Kotlin + Jetpack Compose.

- **Автопоиск**: скан стартует сам, `CO2-Birdy` показывается отдельной карточкой с приоритетом.
- **Визуализация**: стрелочный индикатор положения серво (метки «жива»/«мёртва»), полоса CO₂ с порогом,
  индикатор АКБ (В и %) по кривой Li-Ion.
- **Настройка**: порог CO₂, гистерезис, угол ЖИВА/МЁРТВА (живой предпросмотр через `goto=`),
  время хода, звук, смещение температуры датчика.
- **Тесты**: замер, свип серво, трели вверх/вниз, сброс настроек.

Сборка: Android Studio (File → Open) или `gradle assembleDebug` (JDK 17, Android SDK 34).

> Приложение собиралось в пути с кириллицей — в `gradle.properties` стоит
> `android.overridePathCheck=true`; надёжнее собирать из ASCII-пути.

## Home Assistant

Устройство видно в интеграции **BTHome** (Bluetooth, пассивное сканирование):

| Сущность | Единица |
|---|---|
| Carbon dioxide | ppm |
| Temperature | °C |
| Humidity | % |
| Voltage | В |
| Battery | % |
| Battery low | флаг (< 3.3 В) |

Автоматизации и тонкости — [docs/HOMEASSISTANT.md](docs/HOMEASSISTANT.md).

## Настройка по BLE (birdy-2ble)

Сервис **0xFFF0**. Текстовые команды пишутся в `0xFFF4` (или в `0xFFF1` — прошивка сама отличает
текст от бинарных команд), ответ — в `0xFFF5`:

```
thr=1200 hyst=100 alive=0 dead=100 sec=8 snd=1 toff=0
```

| Ключ | Значение |
|---|---|
| `thr=` | порог CO₂ «смерти», ppm (300…3000) |
| `hyst=` | гистерезис оживления, ppm (0…500) |
| `alive=` / `dead=` | углы серво, −100…100 % |
| `sec=` | время хода серво, с (0…30) |
| `snd=` | звук 0/1 |
| `toff=` | смещение температуры, 0.1 °C (−50…50) |
| `goto=` | сразу поставить серво в положение (подбор углов) |
| `meas` / `sweep` / `up` / `down` / `reset` / `get` / `bat` | тесты и чтение |

Полный протокол (бинарные `FFF1/FFF2/FFF3`, формат `info`):
[firmware/birdy-2ble/docs/CONTROL-2BLE.md](firmware/birdy-2ble/docs/CONTROL-2BLE.md).

Калибровка CO₂ **намеренно не реализована** — датчик калиброван на заводе, автокалибровка (AFC) работает сама.
Настраивается только смещение температуры.

## Настройки сборки (build_flags)

| Флаг | По умолчанию | Что задаёт |
|---|---|---|
| `BIRDY_SERVO_PIN` / `BIRDY_PIEZO_PIN` | 24 / 22 | пины серво и пьезо |
| `BIRDY_CO2_BAD` | 600 | порог CO₂, ppm |
| `BIRDY_STATUS_DEAD_TICKS` / `REVIVE_TICKS` | 1 / 1 | подтверждения перехода |
| `BIRDY_ANGLE_ALIVE` / `ANGLE_DEAD` | 0 / 100 | углы, % |
| `BIRDY_TRANSITION_SEC` | 3 | время хода, с |
| `BIRDY_SELFTEST` / `BIRDY_SELFTEST_MS` | 1 / 8000 | плавный тест серво при загрузке |
| `BIRDY_VBAT_PIN` / `R_TOP` / `R_BOT` / `CAL_PCT` | 31 / 15000 / 15000 / 100 | делитель АКБ (2 × 15 кОм) |
| `BTHOME_PRPH_CONN` | 2 | число одновременных подключений |

## Структура проекта

```
firmware/
  base/          platformio.ini, src/{main.cpp,BTHome.*}, variants/nicenano
  birdy-2ble/    platformio.ini, src/{main.cpp,BirdServo,BirdPiezo,BirdyCtl,battery,BTHome},
                 variants/, docs/CONTROL-2BLE.md
android/
  birdy-app/     build.gradle.kts, app/src/main/java/com/airalab/birdy/*.kt
docs/            INSTALL, WIRING, FLASHING, HOMEASSISTANT, ARCHITECTURE, ENERGY, TROUBLESHOOTING
scripts/         elf2uf2.py
```

## Troubleshooting (короткое)

| Симптом | Причина / решение |
|---|---|
| В HA статичные 400 ppm / 0.0 °C | нет свежих данных с датчика → проверить I²C (SDA P0.17, SCL P0.20) и питание; birdy-2ble сам лечит шину |
| Устройства нет в HA / в приложении | реклама идёт окнами ~30 с раз в минуту — подождать; проверить питание |
| Серво дёргается или гудит | сузить крайние углы (`alive`/`dead`), уменьшить `sec` |
| Постоянный писк | трель зависла → перезагрузка; в актуальной прошивке исправлено |
| Батарея врёт | подобрать `batcal` по мультиметру, поставить 100 нФ с P0.31 на GND |

Полный список: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Документация

- [docs/INSTALL.md](docs/INSTALL.md) — установка инструментов
- [docs/WIRING.md](docs/WIRING.md) — распиновка и схема
- [docs/FLASHING.md](docs/FLASHING.md) — прошивка (UF2 / DFU)
- [docs/HOMEASSISTANT.md](docs/HOMEASSISTANT.md) — интеграция с HA
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — архитектура прошивки
- [docs/ENERGY.md](docs/ENERGY.md) — энергобюджет и автономность
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — типовые проблемы
- [firmware/birdy-2ble/docs/CONTROL-2BLE.md](firmware/birdy-2ble/docs/CONTROL-2BLE.md) — BLE-протокол
- [android/birdy-app/README.md](android/birdy-app/README.md) — приложение

## Contributing

См. [CONTRIBUTING.md](CONTRIBUTING.md) и [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
История изменений — [CHANGELOG.md](CHANGELOG.md).

## Лицензия

MIT — см. [LICENSE](LICENSE).
