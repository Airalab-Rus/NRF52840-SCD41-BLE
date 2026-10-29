#!/usr/bin/env python3
"""
elf2uf2.py — собирает flash-образ (по LMA) из firmware.elf и генерирует
Adafruit nRF52 UF2.

Почему не просто `objcopy -O binary`:
  у nRF52840-приложения VMA секции `.data` лежит в RAM (0x20006000),
  а LMA — во флэше. `objcopy -O binary` строит образ по VMA и даёт
  файл на 512 МБ. Поэтому берём секции с CONTENTS+ALLOC+LOAD, где
  LMA != 0, и склеиваем их по LMA.

Использование:
    python scripts/elf2uf2.py <firmware.elf> <out.uf2> [--base 0x26000]

Требования:
    - Python 3.8+
    - arm-none-eabi-objdump в PATH, либо переменная ARM_OBJDUMP,
      либо установленный PlatformIO (стандартные пути ищутся сами)

Валидация:
    - magic0/magic1 в каждом блоке
    - targetAddr первого блока == base (0x26000)
    - payloadSize == 256
    - familyID == 0xADA52840
    - размер файла == 512 * numBlocks
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

# ===== Константы nRF52 UF2 (формат проверен на реальном бутлоадере) =====
# Формат блока: 512 байт = 32 (заголовок) + 480 (payload: 256 данных + 220 zero + 4 MAGIC_END)
MAGIC0 = 0x0A324655   # "UF2"
MAGIC1 = 0x9E5D5157   # nRF52-specific (НЕ классический 0x9E57A16A!)
MAGIC_END = 0x0AB16F30  # последние 4 байта каждого блока
FLAG_FLUSH = 0x2000
PAYLOAD = 256         # байт данных в блоке (nRF52 page)
BLOCK = 512           # 32 hdr + 480 payload
FAMILY = 0xADA52840
DEFAULT_BASE = 0x26000

# ===== Поиск arm-none-eabi-objdump =====
def find_objdump():
    env = os.environ.get("ARM_OBJDUMP")
    if env and os.path.exists(env):
        return env
    in_path = shutil.which("arm-none-eabi-objdump")
    if in_path:
        return in_path
    home = os.path.expanduser("~")
    candidates = [
        os.path.join(home, ".platformio", "packages",
                     "toolchain-gccarmnoneeabi", "bin", "arm-none-eabi-objdump.exe"),
        os.path.join(home, ".platformio", "packages",
                     "toolchain-gccarmnoneeabi", "bin", "arm-none-eabi-objdump"),
        # типичный путь с версией пакета
        os.path.join(home, ".platformio", "packages",
                     "toolchain-gccarmnoneeabi@1.70201.0", "bin", "arm-none-eabi-objdump.exe"),
        os.path.join(home, ".platformio", "packages",
                     "toolchain-gccarmnoneeabi@1.70201.0", "bin", "arm-none-eabi-objdump"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    # последний шанс: поиск по ~/.platformio
    root = os.path.join(home, ".platformio", "packages")
    if os.path.isdir(root):
        for pkg in os.listdir(root):
            if pkg.startswith("toolchain-gccarmnoneeabi"):
                for arch in ("arm-none-eabi-objdump.exe", "arm-none-eabi-objdump"):
                    p = os.path.join(root, pkg, "bin", arch)
                    if os.path.exists(p):
                        return p
    raise SystemExit("arm-none-eabi-objdump не найден. "
                     "Установите PlatformIO toolchain-gccarmnoneeabi "
                     "или задайте ARM_OBJDUMP.")

# ===== Разбор секций ELF =====
def parse_sections(elf, objdump):
    out = subprocess.check_output([objdump, "-h", elf], text=True)
    secs = []
    lines = out.splitlines()
    i = 0
    while i < len(lines):
        parts = lines[i].split()
        # строка: Idx Name Size VMA LMA File off Algn
        if len(parts) >= 6 and parts[0].isdigit():
            name = parts[1]
            try:
                size = int(parts[2], 16)
                vma = int(parts[3], 16)
                lma = int(parts[4], 16)
                foff = int(parts[5], 16)
            except ValueError:
                i += 1
                continue
            attrs = lines[i + 1] if i + 1 < len(lines) else ""
            if ("CONTENTS" in attrs and "LOAD" in attrs
                    and "ALLOC" in attrs and lma != 0):
                secs.append((name, lma, foff, size))
        i += 1
    if not secs:
        raise SystemExit("Не удалось разобрать секции ELF (objdump -h)")
    return secs

# ===== Сборка flash-образа по LMA =====
def build_flash_image(elf, secs, base):
    blob = open(elf, "rb").read()
    secs = sorted(secs, key=lambda s: s[1])
    first = secs[0][1]
    last = max(s[1] + s[3] for s in secs)
    if first != base:
        raise SystemExit(
            "Первая секция LMA=%#x != base=%#x.\n"
            "Проверьте, что ELF собран под board=nicenano (app base 0x26000)."
            % (first, base))
    image = bytearray(b"\xff" * (last - first))
    for name, lma, foff, size in secs:
        data = blob[foff:foff + size]
        if len(data) != size:
            raise SystemExit("short read в секции %s" % name)
        image[lma - first:lma - first + size] = data
    return bytes(image)

# ===== Генерация UF2 =====
def to_uf2(image, base):
    nblocks = (len(image) + PAYLOAD - 1) // PAYLOAD
    out = bytearray()
    for i in range(nblocks):
        chunk = image[i * PAYLOAD:(i + 1) * PAYLOAD]
        chunk = chunk.ljust(PAYLOAD, b"\x00")
        hdr = struct.pack(
            "<8I",
            MAGIC0,
            MAGIC1,
            FLAG_FLUSH,
            base + i * PAYLOAD,
            PAYLOAD,
            i,
            nblocks,
            FAMILY,
        )
        # payload: 256 данных + 220 zero + 4 MAGIC_END = 480
        payload = chunk + b"\x00" * 220 + struct.pack("<I", MAGIC_END)
        out += hdr + payload
    return bytes(out)

# ===== Валидация =====
def validate_uf2(data, base):
    if len(data) % BLOCK:
        raise SystemExit("UF2: размер файла не кратен 512")
    nblocks = len(data) // BLOCK
    ok = True
    for i in range(nblocks):
        blk = data[i * BLOCK:(i + 1) * BLOCK]
        m0, m1, flags, tgt, psize, bno, nb, fam = struct.unpack("<8I", blk[:32])
        if m0 != MAGIC0 or m1 != MAGIC1:
            ok = False
            print("  block %d: BAD MAGIC (m0=0x%08X m1=0x%08X)" % (i, m0, m1))
            break
        if struct.unpack("<I", blk[508:512])[0] != MAGIC_END:
            ok = False
            print("  block %d: BAD MAGIC_END" % i)
            break
        if psize != PAYLOAD:
            ok = False
            print("  block %d: payloadSize=%d (ожидалось %d)" % (i, psize, PAYLOAD))
        if bno != i or nb != nblocks:
            ok = False
            print("  block %d: blockNo/numBlocks не сходятся" % i)
        if fam != FAMILY:
            ok = False
            print("  block %d: familyID=0x%X (ожидалось 0x%06X)" % (i, fam, FAMILY))
    tgt0 = struct.unpack("<I", data[12:16])[0]
    if tgt0 != base:
        ok = False
        print("  block 0: targetAddr=0x%X != base=0x%X" % (tgt0, base))
    return {"ok": ok, "blocks": nblocks, "tgt0": "0x%X" % tgt0}

# ===== main =====
def main():
    ap = argparse.ArgumentParser(description="ELF -> Adafruit nRF52 UF2")
    ap.add_argument("elf", help="путь к firmware.elf")
    ap.add_argument("dst", help="путь к выходному .uf2")
    ap.add_argument("--base", default=hex(DEFAULT_BASE),
                    help="базовый адрес приложения (по умолчанию 0x26000)")
    args = ap.parse_args()
    base = int(args.base, 16)

    objdump = find_objdump()
    print("objdump:", objdump)
    secs = parse_sections(args.elf, objdump)
    print("flash-секции (LMA, fileoff, size):")
    for name, lma, foff, size in sorted(secs, key=lambda s: s[1]):
        print("  %-14s lma=%#08x off=%#08x size=%#x" % (name, lma, foff, size))
    image = build_flash_image(args.elf, secs, base)
    print("flash image size:", len(image), hex(len(image)))
    uf2 = to_uf2(image, base)
    with open(args.dst, "wb") as f:
        f.write(uf2)
    v = validate_uf2(uf2, base)
    print("uf2 validate:", v)
    if not v["ok"]:
        raise SystemExit("UF2 не прошёл валидацию")
    print("OK ->", args.dst, "(%d байт, %d блоков)" % (len(uf2), v["blocks"]))

if __name__ == "__main__":
    main()
