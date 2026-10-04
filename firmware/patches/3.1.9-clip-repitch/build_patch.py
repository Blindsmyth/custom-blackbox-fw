#!/usr/bin/env python3
"""Build the 3.1.U Clip Repitch image from stock 3.1.9. Does not modify the stock file.

Writes BLACKBOX.BIN, BLACKBOX.sym.json (cave symbols, used by tools/bench) and cave.dis.
Addresses: docs/map-319.md.
"""

import json
import struct
import subprocess
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STOCK = ROOT / "firmware" / "bins" / "3.1.9" / "BLACKBOX.bin"
OUT = HERE / "BLACKBOX.BIN"
SYMS = HERE / "BLACKBOX.sym.json"
CAVE_S = HERE / "cave.S"
OBJ = HERE / "cave.o"
BASE = 0x08040000
CAVE_VA = 0x080F1E80
CAVE_OFF = CAVE_VA - BASE
VERSION_OFF = 0x8F294  # the '9' in "3.1.9"
LETTER = "U"

# (site, cave symbol, bl?, stock bytes at the site)
SITES = [
    (0x0808E1C8, "hook_register", True, "fdf786ff"),   # Param_RegisterAll: last enum
    (0x0809423C, "hook_template", True, "fff75bfe"),   # Param_BagTemplate case 3: last add
    (0x0809AC28, "hook_clip_def", True, "f9f750f9"),   # Clip defaults: beatcount=0
    (0x0804C59C, "hook_post", False, "00b587b0"),      # Engine_PostParam entry (b.w)
    (0x08093ECC, "hook_set", False, "10b5b0f8"),       # Param_Set entry (b.w)
    (0x08067648, "hook_clip_param", False, "f0b5d2f8"), # clip-engine param switch (b.w)
    (0x08064A94, "hook_rate", False, "2de9f043"),      # Clip_RecalcRates entry (b.w)
    (0x08065756, "hook_main_rate", True, "c3ed127a"),  # main-path grain+0x48 store
    (0x080655E6, "hook_grain", True, "fff753fe"),      # slicer Clip_StartGrain site 1
    (0x080657E6, "hook_grain", True, "fff753fd"),      # slicer Clip_StartGrain site 2
]

# Clip Pos scroll list: fifth ushort was 0. Type-0x48 pads use 0x080EF948 (Pos at
# +0x48). Type-0x4C objects use the copy at 0x080EF240. Patch both.
TABLES = [
    (0x080EF998, "9c01", "0000"),
    (0x080EF290, "9c01", "0000"),
]


def encode_b(src, dest, link):
    offset = dest - (src + 4)
    if offset & 1 or not (-(1 << 24) <= offset < (1 << 24)):
        raise ValueError(f"branch from {src:#x} to {dest:#x} out of range ({offset:#x})")
    o = offset & 0x1FFFFFF
    s = (o >> 24) & 1
    i1 = (o >> 23) & 1
    i2 = (o >> 22) & 1
    imm10 = (o >> 12) & 0x3FF
    imm11 = (o >> 1) & 0x7FF
    j1 = (~(i1 ^ s)) & 1
    j2 = (~(i2 ^ s)) & 1
    hw1 = 0xF000 | (s << 10) | imm10
    prefix = 0xC000 if link else 0x8000
    hw2 = prefix | (j1 << 13) | (1 << 12) | (j2 << 11) | imm11
    return struct.pack("<HH", hw1, hw2)


def load_cave(path):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise SystemExit("cave.o is not ELF")
    e_shoff = struct.unpack_from("<I", data, 32)[0]
    e_shentsize = struct.unpack_from("<H", data, 46)[0]
    e_shnum = struct.unpack_from("<H", data, 48)[0]
    e_shstrndx = struct.unpack_from("<H", data, 50)[0]

    def shdr(i):
        return struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize)

    strtab = shdr(e_shstrndx)
    names = data[strtab[4]: strtab[4] + strtab[5]]
    sections = {}
    order = []
    for i in range(e_shnum):
        s = shdr(i)
        name = names[s[0]:].split(b"\0", 1)[0].decode()
        sections[name] = s
        order.append(name)
    text_idx = order.index(".text")
    text = sections[".text"]
    blob = bytearray(data[text[4]: text[4] + text[5]])
    sym = sections[".symtab"]
    strs = sections[".strtab"]
    strbytes = data[strs[4]: strs[4] + strs[5]]
    symbols = []
    off = sym[4]
    for _ in range(sym[5] // sym[9]):
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", data, off)
        off += sym[9]
        symbols.append((strbytes[st_name:].split(b"\0", 1)[0].decode(), st_value, st_shndx))
    for name in order:
        if name.startswith(".rel") and name != ".rel.text":
            raise SystemExit(f"unexpected relocation section {name}")
    rel = sections.get(".rel.text")
    if rel:
        off = rel[4]
        for _ in range(rel[5] // 8):
            r_offset, r_info = struct.unpack_from("<II", data, off)
            off += 8
            typ = r_info & 0xFF
            name, value, shndx = symbols[r_info >> 8]
            if shndx != text_idx:
                raise SystemExit(f"relocation against non-cave symbol {name!r}")
            if typ in (10, 30):
                blob[r_offset:r_offset + 4] = encode_b(CAVE_VA + r_offset, CAVE_VA + (value & ~1), typ == 10)
            elif typ == 2:
                addend = struct.unpack_from("<I", blob, r_offset)[0]
                struct.pack_into("<I", blob, r_offset, CAVE_VA + value + addend)
            else:
                raise SystemExit(f"unhandled relocation type {typ} at {r_offset:#x}")
    resolved = {}
    for name, value, shndx in symbols:
        if name and not name.startswith("$") and not name.startswith(".") and shndx == text_idx:
            resolved[name] = CAVE_VA + (value & ~1)
    return bytes(blob), resolved


def main():
    subprocess.check_call(["clang", "-target", "armv7em-none-eabi", "-mthumb",
                           "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16",
                           "-c", "-o", str(OBJ), str(CAVE_S)])
    blob, symbols = load_cave(OBJ)
    if len(blob) > 0x2000:
        raise SystemExit(f"cave is unexpectedly large: {len(blob):#x}")
    stock = STOCK.read_bytes()
    image = bytearray(stock)
    if len(image) > CAVE_OFF:
        raise SystemExit("stock image overlaps the cave")
    image.extend(b"\x00" * (CAVE_OFF - len(image)))
    image.extend(blob)

    print("cave", hex(CAVE_VA), "size", hex(len(blob)))
    for name in sorted(symbols, key=symbols.get):
        print(f"  {name:14} {symbols[name]:#010x}")
    for va, symbol, link, orig in SITES:
        off = va - BASE
        if image[off:off + 4].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 4].hex()} != {orig}")
        image[off:off + 4] = encode_b(va, symbols[symbol], link)
        print(f"  {'bl' if link else 'b.w':4} {va:#010x} -> {symbol}")

    for va, new, orig in TABLES:
        off = va - BASE
        if image[off:off + 2].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 2].hex()} != {orig}")
        image[off:off + 2] = bytes.fromhex(new)
        print(f"  u16  {va:#010x} -> 0x19C")

    if image[VERSION_OFF] != ord("9"):
        raise SystemExit(f"version byte is {image[VERSION_OFF]:#x}, expected '9'")
    image[VERSION_OFF] = ord(LETTER)

    verify(image, stock, symbols)
    OUT.write_bytes(image)
    SYMS.write_text(json.dumps({k: f"{v:#010x}" for k, v in symbols.items()}, indent=1) + "\n")
    print("wrote", OUT, "size", len(image))


def verify(image, stock, symbols):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    insns = list(md.disasm(bytes(image[CAVE_OFF:]), CAVE_VA))
    (HERE / "cave.dis").write_text(
        "\n".join(f"{i.address:#010x}: {i.mnemonic:8} {i.op_str}" for i in insns) + "\n")
    for va, symbol, link, _ in SITES:
        insn = next(md.disasm(bytes(image[va - BASE: va - BASE + 4]), va))
        if insn.mnemonic not in ("bl", "b.w", "b") or int(insn.op_str.lstrip("#"), 16) != symbols[symbol]:
            raise SystemExit(f"{va:#x} decoded as {insn.mnemonic} {insn.op_str}")
    for va, new, _ in TABLES:
        if image[va - BASE: va - BASE + 2].hex() != new:
            raise SystemExit(f"{va:#x}: table bytes {image[va - BASE: va - BASE + 2].hex()}")
    allowed = {VERSION_OFF}
    for va, *_ in SITES:
        allowed.update(range(va - BASE, va - BASE + 4))
    for va, *_ in TABLES:
        allowed.update(range(va - BASE, va - BASE + 2))
    extra = [i for i, (a, b) in enumerate(zip(stock, image)) if a != b and i not in allowed]
    if extra:
        raise SystemExit(f"unexpected diffs in the stock part: {[hex(i) for i in extra[:12]]}")
    print("verify ok")


if __name__ == "__main__":
    main()
