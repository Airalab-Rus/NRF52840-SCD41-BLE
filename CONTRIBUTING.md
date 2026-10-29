# Contributing

Спасибо, что хотите помочь! Проект маленький (≈1000 строк C++), поэтому
правила простые.

## Как внести изменения

1. Сделайте fork и выделите ветку:
   ```bash
   git checkout -b feat/your-feature   # или fix/your-bug, docs/your-doc
   ```
2. Соберите проект и убедитесь, что всё работает:
   ```bash
   pio run
   ```
3. Проверьте, что не затронули [known limitations](docs/TROUBLESHOOTING.md#known-limitations)
   и не вернули баг двойного `readMeasurement()` (см. `docs/ARCHITECTURE.md`, раздел «История бага»).
4. Коммиты — [Conventional Commits](https://www.conventionalcommits.org/ru-hyphen/):
   - `feat: …` — новая возможность
   - `fix: …` — исправление бага
   - `docs: …` — только документация
   - `chore: …` — сборка/инструменты
   - `refactor: …` — переписывание без изменения поведения
5. Откройте PR на `main` с описанием: что изменилось, почему, как проверено.

## Что проверяется в PR

- `pio run` в CI (обязательно зелёный)
- отсутствие секретов (IP-адреса, токены, ключи)
- наличие лицензионных заголовков в новых файлах

## Чего избегать

- Не «чините» BTHome.cpp/BTHome.h без необходимости — это проверенная портировка
  чужого кода (MIT), и она работает.
- Не добавляйте второй `readMeasurement()` в `doMeasure()` — см. историю бага v0.2.
- Не коммитьте артефакты: `.pio/`, `*.uf2`, логи.

## Локальная «тест-лаборатория»

```bash
pio run                 # сборка
pio device monitor      # UART-лог 115200
python scripts/elf2uf2.py .pio/build/nicenano/firmware.elf out.uf2   # UF2
# двойной RESET → копировать out.uf2 в диск NICENANO
```
