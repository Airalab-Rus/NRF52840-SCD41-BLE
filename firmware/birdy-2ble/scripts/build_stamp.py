# Генерирует src/build_stamp.h с датой/временем сборки.
# Подключён в platformio.ini как extra_scripts = pre:scripts/build_stamp.py
import datetime

Import("env")  # noqa: F821

stamp = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M")
path = "src/build_stamp.h"
content = (
    "/* Автоматически сгенерировано scripts/build_stamp.py — не править. */\n"
    "#pragma once\n"
    '#define BIRDY_BUILD "%s"\n' % stamp
)
with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(content)
print("[build_stamp] BIRDY_BUILD =", stamp)
