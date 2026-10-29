# Прошивка (Flashing)

## Память nRF52840 (карта)

```
0x000000 ─┬─ Adafruit nRF52 UF2 bootloader
0x005000  ├─ SoftDevice S140 v6.1.1   (BLE-стек, не перепрошивать!)
0x026000 ─┴─ НАШЕ ПРИЛОЖЕНИЕ (app base = 0x26000)
```

UF2-бутлоадер и SoftDevice **уже на чипе** — прошивать нужно только
приложение, и только в область `0x26000+`.

## Как попасть в режим загрузки (bootloader)

**Двойное нажатие RESET** (коротко-коротко) при подключённом USB.
Появляется USB-диск **`NICENANO`** (у нас — буква `E:`).

> Если диск появляется и пропадает — копировать файл **сразу** после
> появления (файл ~260 КБ, до 5 с хватает). Подробности —
> [TROUBLESHOOTING](TROUBLESHOOTING.md#диск-nicenano-пропадает-при-копировании).

## Генерация UF2 из ELF

```bash
pio run
python scripts/elf2uf2.py .pio/build/nicenano/firmware.elf CO2-AirNode.uf2
```

`scripts/elf2uf2.py`:
- читает секции `firmware.elf` через `arm-none-eabi-objdump` (путь задаётся
  переменной окружения `ARM_OBJDUMP`, иначе ищется в `PATH` и в стандартных
  местах PlatformIO),
- склеивает секции по **LMA** (важно: `objcopy -O binary` даёт неправильный
  файл из-за VMA `.data` = 0x20006000),
- пишет Adafruit nRF52 UF2 (family `0xADA52840`, target `0x26000`, 256-байтные блоки),
- валидирует результат (magic, tgt0, размер).

Проверка размера: ожидаем **135328 байт** flash-образа (16.6 % флэша).

## Копирование

- Windows: drag-and-drop `CO2-AirNode.uf2` в окно диска, **или**
  ```powershell
  Copy-Item .\CO2-AirNode.uf2 E:\
  ```
- Linux/macOS: `cp CO2-AirNode.uf2 /media/$USER/NICENANO/`
- После копирования диск отмонтируется, плата **сама перезагрузится** в приложение.

## Мониторинг

```bash
pio device monitor --baud 115200
# или: pio device monitor   (115200 из platformio.ini)
```

Ожидаемый старт:

```
=== CO2-AirNode boot ===
[boot] BLE begin done
[boot] SCD41 begin: OK
[boot] first measure: OK co2=412 temp=23.4C rh=41.3%
[meas] RAW sensor: co2=412 ppm temp=23.41C rh=41.28%
```

## Переписать бутлоадер (только если сломан)

1. Двойной RESET (или удержание RESET при подключении USB)
2. В диск `NICENANO` скопировать официальный файл
   **Adafruit nRF52840 UF2 bootloader** (с https://learn.adafruit.com/adafruit-nrf52840-qt-protobox)
3. После перезагрузки — прописать SoftDevice S140 (официальный UF2 от Nordic)
4. И только потом — наше приложение

## Почему не `pio run -t upload`

Платформа `nordicnrf52` не умеет прошивать nRF52840 SuperMini по USB-сериалу
(нет CMSIS-DAP на плате). UF2 — штатный способ для этого железа.
Если нужен J-Link/Black Magic — это отдельная задача, не в scope проекта.
