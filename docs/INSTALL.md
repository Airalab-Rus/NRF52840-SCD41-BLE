# Установка с нуля

Целевая ОС в нашем стенде — **Windows 11** (Git Bash / PowerShell).
Всё то же работает на Linux/macOS, отличий практически нет.

## 1. Python

Нужен **Python 3.9–3.12** (PlatformIO Core — это Python-приложение).

- Windows: https://www.python.org/downloads/ → галка **«Add python.exe to PATH»**
- Проверка:
  ```bash
  python --version    # Python 3.11.x — ок
  ```

## 2. PlatformIO Core

```bash
python -m pip install --upgrade platformio
pio --version         # PlatformIO Core, version 6.x.x
```

Альтернативы (необязательно): VS Code + расширение PlatformIO IDE —
только для удобства, сама сборка идёт тем же `pio run`.

## 3. Платформа и библиотеки

**Не нужны ручные шаги.** При первом `pio run` PlatformIO сам скачает:

- платформу `nordicnrf52` (dirkth/nordicnrf52, Arduino-ядро Adafruit)
- библиотеку `sparkfun/SparkFun SCD4x Arduino Library@^1.1.2`

Кэшируются в `~/.platformio/` (Windows: `C:\Users\<you>\.platformio`).

### Офлайн-сборка (опционально)

Если кэш платформ уже есть (у нас — есть), сборка работает без интернета:
PlatformIO не выходит в сеть, если все зависимости уже скачаны.

## 4. Клон и сборка

```bash
git clone https://github.com/Airalab-Rus/NRF52840-SCD41-BLE.git
cd NRF52840-SCD41-BLE
pio run
```

Ожидаемый хвост лога:

```
RAM:   [=         ]   6.3% (used 15636 bytes from 248832 bytes)
Flash: [==        ]  16.6% (used 135328 bytes from 815104 bytes)
========================= [SUCCESS] Took 10.52 seconds =========================
```

Размер прошивки 135328 байт (0x210a0) — эталонный размер рабочей версии.

## 5. Инструменты для прошивки

- **Ничего дополнительно не нужно** — прошивка идёт через USB-mass-storage
  (двойной RESET → диск `NICENANO`), см. [FLASHING.md](FLASHING.md).
- Для генерации UF2: `scripts/elf2uf2.py` требует только Python +
  `arm-none-eabi-objdump` (уже есть в PlatformIO-пакете toolchain-gccarmnoneeabi;
  путь настраивается переменной `ARM_OBJDUMP`).

## 6. Home Assistant

- HA Core/Container ≥ 2023.x
- BLE-адаптер (на Raspberry Pi: USB-BT-адаптер, BlueZ ≥ 5.66)
- Настройка — [HOMEASSISTANT.md](HOMEASSISTANT.md)
