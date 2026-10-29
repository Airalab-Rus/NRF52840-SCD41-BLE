# Changelog

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
версионирование — [Semantic Versioning](https://semver.org/lang/ru/).
Коммиты — [Conventional Commits](https://www.conventionalcommits.org/ru-hyphen/).

## [1.0.0] - 2026-10-29

### Added

- Публичный релиз: полная документация (README + `docs/`), `LICENSE` (MIT),
  `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `CHANGELOG.md`
- CI: GitHub Actions (`.github/workflows/build.yml`) — сборка `pio run` на каждый push
- `scripts/elf2uf2.py` — самодостаточный скрипт ELF → Adafruit nRF52 UF2
  (без привязки к конкретному рабочему станку)
- `docs/ENERGY.md` — энергобюджет и формула расчёта автономности

### Changed

- Репозиторий переименован: `NRF52840-SCD41-zigbee` → `NRF52840-SCD41-BLE`
  (стек проекта — BLE/BTHome, Zigbee в проекте не используется)

## [0.2.0] - 2026-10-29

### Fixed

- **fix**: убран лишний второй `readMeasurement()` после цикла ожидания —
  корневая причина статичных «400 ppm / 0.0 °C / 0.0 %» в Home Assistant.
  В low-power-режиме SCD4x буфер результата опустошается при чтении,
  повторный `readMeasurement()` возвращал `false` (до следующего результата
  ~1.5 с) и реальные значения отбрасывались в пользу значений по умолчанию

### Added

- feat: первый замер выполняется сразу после boot (быстрое подтверждение данных)
- feat: лог-строка `RAW sensor: co2=.. temp=.. rh=..` — реальные значения из библиотеки
- feat: sanity-check диапазона (100–5000 ppm, −40…60 °C, 0…100 %):
  заведомо мусорные значения отбрасываются, сохраняется последний валидный замер

## [0.1.0] - 2026-10-27

### Added

- feat: первая рабочая сборка: nRF52840 SuperMini (Nice!Nano) + SCD41,
  BTHome v2 over BLE (Arduino/Adafruit core, SoftDevice S140 v6,
  библиотека SparkFun SCD4x)
- feat: цикл «замер → BTHome-пакет → сон» с периодом 60 с
