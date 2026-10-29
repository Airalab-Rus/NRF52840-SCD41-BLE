# NRF52840-SCD41-BLE — CO₂ AirNode для Home Assistant

Пассивный BLE-датчик качества воздуха (CO₂ ppm, температура, влажность) на
**nRF52840 SuperMini (Nice!Nano) + SCD41 (Sensirion)**. Работает от литиевой
батареи, каждые 60 секунд шлёт замер через **BTHome v2** — Home Assistant
видит устройство сам, без хаба, без MQTT, без облака.

![demo](docs/demo.gif)

[![Build](https://github.com/Airalab-Rus/NRF52840-SCD41-BLE/actions/workflows/build.yml/badge.svg)](https://github.com/Airalab-Rus/NRF52840-SCD41-BLE/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: PlatformIO](https://img.shields.io/badge/PlatformIO-nordicnrf52-blue.svg)](https://platformio.org/)

---

## Features

- **SCD41** (NDIR CO₂ + T/RH) по I²C: low-power periodic mode, 1 замер ≈ 1.5 с внутри датчика
- **BTHome v2** — стандартные сенсоры HA: температура (0.01 °C), влажность (0.01 %), CO₂ (ppm), батарея %
- **Без хаба и MQTT**: пассивная BLE-реклама, HA ловит устройство сам (Settings → Bluetooth)
- **Питание от LiPo 3.7 В** (2500 мА·ч): расчёт автономности ≈ **6–8 месяцев** при цикле 60 с — [docs/ENERGY.md](docs/ENERGY.md)
- Датчик между замерами в low-power, I²C отключается (`Wire.end()`), BLE включается только в 0.5 с окна отчёта
- **Простая прошивка**: UF2 (двойной RESET → копирование файла), без J-Link
- CI-сборка (GitHub Actions) + чистая офлайн-сборка через PlatformIO
- Код на C++ (Arduino-ядро), комментарии на русском, вся логика в одном `main.cpp`

## Установка (Installation)

**ОС:** Windows 10/11, Linux, macOS (всё, что поддерживает PlatformIO).
**Язык:** Python **3.9–3.12** (обязательно, нужен для PlatformIO Core).

```bash
# 1. Python из официального python.org (поставить «Add to PATH»)
python --version            # ожидаем 3.9+

# 2. PlatformIO Core
python -m pip install --upgrade platformio

# 3. Клонировать проект
git clone https://github.com/Airalab-Rus/NRF52840-SCD41-BLE.git
cd NRF52840-SCD41-BLE

# 4. Сборка (при первом запуске PlatformIO сам скачает платформу nordicnrf52
#    и библиотеку SparkFun SCD4x — нужен интернет)
pio run
```

Успех: `SUCCESS`, Flash ≈ 135 KB (16.6 %), RAM ≈ 6.3 %.
Подробности и офлайн-режим — [docs/INSTALL.md](docs/INSTALL.md).

## Quick start (за 5 минут)

1. **Распаять датчик** (4 провода): SDA→**P0.17**, SCL→**P0.20**, VCC→**3V3**, GND→**GND** — [docs/WIRING.md](docs/WIRING.md)
2. `pio run` → собрать `.pio/build/nicenano/firmware.elf`
3. `python scripts/elf2uf2.py .pio/build/nicenano/firmware.elf CO2-AirNode.uf2` → UF2-файл
4. **Двойной RESET** на плате → появится диск `NICENANO` (E:) → копировать `CO2-AirNode.uf2` туда
5. Плата перезагрузится в приложение. В Home Assistant: **Настройки → Устройства и службы → + → Bluetooth** — устройство `CO2-AirNode` найдено, сенсоры появляются в течение минуты
6. Готово. Каждый цикл (60 с) — новый пакет с реальными CO₂/T/RH

Пошаговая прошивка — [docs/FLASHING.md](docs/FLASHING.md), настройка HA — [docs/HOMEASSISTANT.md](docs/HOMEASSISTANT.md).

## Usage

Сценарии «из коробки»:

- **Дашборд HA**: карточки `temperature`, `humidity`, `carbon_dioxide`, `battery` обновляются сами (пакет приходит каждые 60 с, HA обновляет значения при приёме пакета).
- **Удаление устройства**: HA → Настройки → Устройства → `CO2-AirNode` → удалить (или отключить Bluetooth-адаптер на 10 с).
- **Смена имени/периода замера**: отредактировать `#define` в `src/main.cpp`, пересобрать, переписать — см. [Configuration](#configuration).
- **Отладка по USB-серии**: `pio device monitor` (115200) — в логе видно каждый цикл:
  ```
  === CO2-AirNode boot ===
  [boot] SCD41 begin: OK
  [boot] first measure: OK co2=412 temp=23.4C rh=41.3%
  [meas] RAW sensor: co2=412 ppm temp=23.41C rh=41.28%
  ```

## Configuration

Все настройки — `#define` в начале [`src/main.cpp`](src/main.cpp) (это «конфигурационный файл» проекта; `.env`/env-переменные для прошивки не используются):

| Настройка | Файл | По умолчанию | Описание |
|---|---|---|---|
| `MY_SDA_PIN` | `src/main.cpp` | `17` | I²C-SDA SCD41 (P0.17) |
| `MY_SCL_PIN` | `src/main.cpp` | `20` | I²C-SCL SCD41 (P0.20) |
| `DEVICE_NAME` | `src/main.cpp` | `"CO2-AirNode"` | имя устройства в BLE/HA |
| `SCD4x_ALTITUDE` | `src/main.cpp` | `160` | высота над у.м., м (коррекция CO₂) |
| `SCD4x_TEMP_OFFSET` | `src/main.cpp` | `0` | сдвиг температуры, °C |
| `MEASUREMENT_PERIOD` | `src/main.cpp` | `60` | период цикла замер+отчёт, **сек** (300 → автономность ×4) |
| `lib_deps` | `platformio.ini` | SparkFun SCD4x `^1.1.2` | версия драйвера датчика |
| `board` | `platformio.ini` | `nicenano` | nRF52840 SuperMini / Nice!Nano |

## Project structure

```
.
├── platformio.ini          # env: nicenano (nordicnrf52 + Arduino + SCD4x)
├── src/
│   ├── main.cpp            # вся логика: boot, замер, BTHome-отчёт (156 строк)
│   ├── BTHome.cpp/.h       # BTHome v2 over BLE (портировка Chreece, без изменений)
├── variants/nicenano/      # Arduino-вариант платы (пины nRF52840 SuperMini)
├── scripts/
│   └── elf2uf2.py          # ELF → UF2 (прошивка через USB-mass-storage)
├── docs/                   # INSTALL / WIRING / FLASHING / HOMEASSISTANT /
│   │                       # ARCHITECTURE / ENERGY / TROUBLESHOOTING
├── .github/workflows/build.yml   # CI: pio run
├── CHANGELOG.md / CONTRIBUTING.md / CODE_OF_CONDUCT.md
└── LICENSE                 # MIT
```

## API reference

### Класс `BTHome` (`src/BTHome.h`)

| Метод | Назначение |
|---|---|
| `begin(name, encryption, key, trigger)` | инициализация BLE + устройства (у нас: без шифрования) |
| `resetMeasurement()` | очистить буфер пакетов перед новым отчётом |
| `addMeasurement(id, value)` | добавить метрику (`ID_TEMPERATURE_PRECISE`, `ID_HUMIDITY_PRECISE`, `ID_CO2`, `ID_BATTERY`, …) |
| `addMeasurement_state(id, state)` | бинарное состояние (`STATE_BATTERY_LOW`, …) |
| `sendPacket(delay_ms)` | собрать пакет, включить advertise на `delay_ms`, отключить |
| `start()/stop()/isAdvertising()` | ручное управление рекламой |
| `buildPacket()` | сборка 31-байтового ADV-пакета (безопасно вызвать вручную) |

Используемые BTHome-идентификаторы: `ID_PACKET=0x00`, `ID_TEMPERATURE_PRECISE=0x02`,
`ID_HUMIDITY_PRECISE=0x03`, `ID_CO2=0x12`, `ID_BATTERY=0x01`, `STATE_BATTERY_LOW=0x15`.

### Цикл работы (main loop)

```
boot → BLE.begin → I2C begin (P0.17/P0.20) → SCD41.begin
     → startLowPowerPeriodicMeasurement
     → до готовности readMeasurement (≤120 с) → чтение глобалов библиотеки
     → stopPeriodicMeasurement + Wire.end()
     → sendReport() (BTHome-пакет, 0.5 с advertise)
     → сон (delay) до следующего цикла (MEASUREMENT_PERIOD)
```

⚠️ После успешного `readMeasurement()` **не делать второй** — в low-power-режиме
буфер датчика пуст между обновлениями, повторный вызов вернёт `false` и реальные
значения потеряются (это был главный баг v0.1, исправлен в v0.2). Подробности —
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Testing

- **CI**: каждый push собирает проект (`pio run`) — GitHub Actions, `.github/workflows/build.yml`.
- **Юнит-тестов нет** (железная прошивка). Покрытие: сборка + ручной стенд:
  1. `pio run` — компиляция/линковка без ошибок
  2. Прошивка UF2, `pio device monitor` — в логе `SCD41 begin: OK` и `RAW sensor: co2=…` с реальными числами
  3. HA: значения CO₂/T/RH меняются от дыхания/тепловых пушек (а не статичные 400/0/0)
- Чек-лист приёмки — [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Build & deploy

| Шаг | Команда |
|---|---|
| Сборка | `pio run` |
| UF2 из ELF | `python scripts/elf2uf2.py .pio/build/nicenano/firmware.elf CO2-AirNode.uf2` |
| Прошивка | двойной RESET → копировать `CO2-AirNode.uf2` в диск `NICENANO` |
| Мониторинг | `pio device monitor --baud 115200` |
| CI | push → GitHub Actions (автоматическая `pio run`) |

Менять код → пересобирать → переписывать UF2. OTA-DFU в этой версии **нет**
(см. [Known limitations](docs/TROUBLESHOOTING.md#known-limitations)).

## Troubleshooting

Коротко (полная таблица — [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)):

| Симптом | Решение |
|---|---|
| В HA статично 400 ppm / 0.0 °C / 0.0 % | прошивка v0.1.0 (баг двойного чтения). Перепрошить v1.0.0 |
| `SCD41 begin: FAILED` | I²C-пины (SDA=17, SCL=20), VCC/GND, перепайка; опционально внешние pull-up 4.7 кОм |
| HA не находит устройство | BLE-адаптер включён, плата запитана, в логе есть `BLE begin done` |
| Батарея всегда 100 % | делитель напряжения не распаян — значение условное (см. limitations) |
| Диск `NICENANO` пропадает при копировании | копировать сразу после появления диска (файл ~260 КБ) |

## Contributing

Ветки `bionic/…` или `feat/…`, коммиты в стиле [Conventional Commits](https://www.conventionalcommits.org/ru-hyphen/), PR — на `main`. Полные правила — [CONTRIBUTING.md](CONTRIBUTING.md). Поведение в проекте — [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) (c) 2026 Airalab-Rus. Зависимости и их лицензии — в конце LICENSE.

## Доки

- [docs/INSTALL.md](docs/INSTALL.md) — установка с нуля
- [docs/WIRING.md](docs/WIRING.md) — распиновка
- [docs/FLASHING.md](docs/FLASHING.md) — прошивка UF2
- [docs/HOMEASSISTANT.md](docs/HOMEASSISTANT.md) — настройка HA
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — архитектура + история бага
- [docs/ENERGY.md](docs/ENERGY.md) — энергобюджет и расчёт автономности
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — ошибки и решения
