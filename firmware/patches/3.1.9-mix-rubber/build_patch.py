#!/usr/bin/env python3
"""Build 3.1.R: 3.1.Q plus Warp Rubber (Rubber Band in flash bank 2).

Outputs, next to this file:
  BLACKBOX.BIN     bank 1 image for the SD installer (stock 3.1.9 + cave)
  BBRUBBER.BIN     bank 2 payload; the card loader (tools/bank2/b2load.c) programs it on first boot
  BLACKBOX.sym.json, cave.dis, rubber.map

Reuses ../3.1.9-folders-repitch-mix/build_patch.py (its patch lists, collision/alignment checks and
verify) and adds the Rubber sites. Needs the Arm GNU Toolchain with newlib/libstdc++ (ARM_GNU, default
~/.local/arm-gnu-15.3/bin), clang for the cave asm, and tools/stretch/rubber/fetch_rubberband.sh run once.
"""

import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MIX_DIR = HERE.parent / "3.1.9-folders-repitch-mix"
spec = importlib.util.spec_from_file_location("mix_build", MIX_DIR / "build_patch.py")
mix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mix)
base = mix.base

RUBBER = ROOT / "tools" / "stretch" / "rubber"
BANK2 = ROOT / "tools" / "bank2"
CMSIS = RUBBER / "cmsis"
RB = RUBBER / "rubberband"
BUILD = RUBBER / "build"
T = Path(os.environ.get("ARM_GNU", Path.home() / ".local" / "arm-gnu-15.3" / "bin"))
GCC, GXX = T / "arm-none-eabi-gcc", T / "arm-none-eabi-g++"

OUT = HERE / "BLACKBOX.BIN"
OUT_B2 = HERE / "BBRUBBER.BIN"
SYMS = HERE / "BLACKBOX.sym.json"
ELF = BUILD / "rubber.elf"
if "--emu" in sys.argv:
    OUT, OUT_B2, SYMS = BUILD / "BLACKBOX_emu.BIN", BUILD / "BBRUBBER_emu.BIN", BUILD / "BLACKBOX_emu.sym.json"
    ELF = BUILD / "rubber_emu.elf"
LETTER = "R"
B2_BASE, B2_LIMIT = 0x08100000, 0xC0000

CPU = ["-mthumb", "-mcpu=cortex-m7", "-mfloat-abi=hard"]
BANK1_C = CPU + ["-mfpu=fpv5-sp-d16", "-Os", "-ffreestanding", "-fno-builtin", "-nostdlib",
                 "-fno-tree-loop-distribute-patterns", "-ffunction-sections", "-Wall", "-Wextra"]
# --emu: bank 2 for the single-precision FPU (doubles in software) because Unicorn's Cortex-M7
# has no double-precision unit. Same sources; written to build/ for tools/bench, never for a card.
EMU = "--emu" in sys.argv
BANK2_FLAGS = CPU + ["-mfpu=fpv5-sp-d16" if EMU else "-mfpu=fpv5-d16", "-O2", "-ffunction-sections",
                     "-fdata-sections"]
CMSIS_SRC = [CMSIS / "Source" / "TransformFunctions" / f for f in (
    "arm_rfft_fast_f32.c", "arm_rfft_fast_init_f32.c", "arm_cfft_f32.c", "arm_cfft_init_f32.c",
    "arm_cfft_radix8_f32.c", "arm_bitreversal.c", "arm_bitreversal2.c")] + \
    [CMSIS / "Source" / "CommonTables" / f for f in ("arm_common_tables.c", "arm_const_structs.c")]
B2_SOURCES = [RUBBER / f for f in ("rubber.cpp", "rubber.h", "rt.c", "RubberBandBB.cpp", "fast_trig.cpp",
                                   "fft_cmsis_impl.inc", "rubberband-blackbox.patch")]


def run(cmd):
    subprocess.check_call([str(c) for c in cmd])


def b2_id():
    h = hashlib.sha256()
    for f in B2_SOURCES:
        h.update(f.read_bytes())
    return int(h.hexdigest()[:8], 16)


def compile_all(rb_id):
    if not (RB / "single" / "RubberBandSingle.cpp").exists():
        raise SystemExit("Rubber Band missing: run tools/stretch/rubber/fetch_rubberband.sh")
    BUILD.mkdir(exist_ok=True)
    objs = []
    cave_o = BUILD / "rubber_cave.o"
    run(["clang", "-target", "armv7em-none-eabi", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16",
         "-I", MIX_DIR, "-I", mix.BASE_DIR, "-c", "-o", cave_o, HERE / "rubber_cave.S"])
    objs.append(cave_o)
    for src in ("b2pair.c", "b2load.c"):
        o = BUILD / src.replace(".c", ".o")
        run([GCC, *BANK1_C, "-I", BANK2, "-I", RUBBER, "-c", "-o", o, BANK2 / src])
        objs.append(o)
    defs = [f"-DRB_ID={rb_id:#x}u"]
    inc = ["-I", RUBBER, "-I", RB, "-I", RB / "src", "-I", CMSIS / "Include"]
    for src in ("RubberBandBB.cpp", "fast_trig.cpp", "rubber.cpp"):
        o = BUILD / src.replace(".cpp", ".o")
        extra = ["-DNO_EXCEPTIONS", "-DPROCESS_SAMPLE_TYPE=float", "-DUSE_APPROXIMATE_ATAN2",
                 "-DUSE_FAST_PHASOR", "-DHAVE_CMSIS_DSP"] if src == "fast_trig.cpp" else []
        run([GXX, *BANK2_FLAGS, "-std=c++17", *defs, *extra, *inc, "-c", "-o", o, RUBBER / src])
        objs.append(o)
    o = BUILD / "rt.o"
    run([GCC, *BANK2_FLAGS, "-c", "-o", o, RUBBER / "rt.c"])
    objs.append(o)
    for src in CMSIS_SRC:
        o = BUILD / ("cmsis_" + src.stem + ".o")
        run([GCC, *BANK2_FLAGS, "-DARM_MATH_CM7", "-D__FPU_PRESENT=1", "-I", CMSIS / "Include",
             "-c", "-o", o, src])
        objs.append(o)
    return objs


def bank1_has_no_ram(objs):
    """Cave code has nowhere to keep .data/.bss: they would land in bank 2's SRAM1 block."""
    for o in objs[:3]:
        out = subprocess.check_output([str(T / "arm-none-eabi-size"), "-A", str(o)], text=True)
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith((".data", ".bss")) and int(parts[1]) > 0:
                raise SystemExit(f"{o.name}: {parts[0]} is {parts[1]} bytes; bank 1 code cannot have RAM")


def link(objs):
    run([GXX, *BANK2_FLAGS, "-nostartfiles", "-T", HERE / "rubber.ld", "-Wl,--gc-sections",
         f"-Wl,-Map={ELF.with_suffix('.map')}", "-o", ELF, *objs, "-lstdc++", "-lm", "-lc", "-lgcc"])
    out = subprocess.check_output([str(T / "arm-none-eabi-nm"), str(ELF)], text=True)
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    cave = BUILD / "cave.bin"
    b2 = BUILD / "bank2.bin"
    run([T / "arm-none-eabi-objcopy", "-O", "binary", "-j", ".cave", ELF, cave])
    run([T / "arm-none-eabi-objcopy", "-O", "binary", "-j", ".b2head", "-j", ".b2text", "-j", ".ARM.extab",
         "-j", ".ARM.exidx", "-j", ".b2data", ELF, b2])
    return bytearray(cave.read_bytes()), bytearray(b2.read_bytes()), syms


def section_vma(name):
    out = subprocess.check_output([str(T / "arm-none-eabi-objdump"), "-h", str(ELF)], text=True)
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 4 and p[1] == name:
            return int(p[3], 16)
    raise SystemExit(f"no section {name}")


def finish_bank2(b2, rb_id):
    """Pad to whole flash words, fill length and sum into the header (rubber.h RbHeader)."""
    while len(b2) % 32:
        b2.append(0)
    if len(b2) > B2_LIMIT:
        raise SystemExit(f"bank 2 payload {len(b2):#x} bytes, more than sectors 0-5 ({B2_LIMIT:#x})")
    magic, hid = struct.unpack_from("<II", b2, 0)
    if magic != 0x32424252 or hid != rb_id:
        raise SystemExit(f"bank 2 header wrong: magic {magic:#x} id {hid:#x}")
    struct.pack_into("<I", b2, 8, len(b2))
    total = sum(struct.unpack_from(f"<{(len(b2) - 16) // 4}I", b2, 16)) & 0xFFFFFFFF
    struct.pack_into("<I", b2, 12, total)
    return total


def stamp_cave(cave, syms, rb_id, total):
    for name, value in (("rb_expect_id", rb_id), ("rb_expect_sum", total)):
        at = syms[name] - base.CAVE_VA
        if cave[at:at + 4] != b"\xff\xff\xff\xff":
            raise SystemExit(f"{name} placeholder not where the symbol says")
        struct.pack_into("<I", cave, at, value)


def no_direct_calls_into_bank2(cave):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    for i in md.disasm(bytes(cave), base.CAVE_VA):
        if i.mnemonic.split(".")[0] in ("bl", "b", "blx") and i.op_str.startswith("#"):
            target = int(i.op_str[1:], 16)
            if target >= B2_BASE:
                raise SystemExit(f"{i.address:#x}: {i.mnemonic} {i.op_str} goes into bank 2 directly")


def stock_bl(stock, va, dest):
    off = va - base.BASE
    want = base.encode_b(va, dest, True).hex()
    if stock[off:off + 4].hex() != want:
        raise SystemExit(f"{va:#x}: stock is {stock[off:off + 4].hex()}, expected bl {dest:#x}")
    return want


def add_rubber_sites(stock):
    mix.SITES.extend([
        (0x080667B8, "hook_lane", False, "2de9f04f"),            # per-lane render: Rubber renders
        *[(va, "b2load_thunk", True, stock_bl(stock, va, 0x08092F5C))
          for va in (0x0807443E, 0x0807446E, 0x080744D8)],     # pcmStreamer file steps: card loader
        (0x08044498, "sdram_top_thunk", False, "4ff04442"),      # sample stack empty: top 0xC3F00000
    ])
    mix.BYTES.append((0x080F1E08, "0000f0c3", "000000c4"))      # .data: sample stack top at boot


def main():
    rb_id = b2_id()
    objs = compile_all(rb_id)
    bank1_has_no_ram(objs)
    cave, b2, syms = link(objs)
    if section_vma(".cave") != base.CAVE_VA or section_vma(".b2head") != B2_BASE:
        raise SystemExit("sections not where the image expects them")
    if base.CAVE_VA + len(cave) > B2_BASE:
        raise SystemExit(f"cave {len(cave):#x} bytes runs into bank 2")
    no_direct_calls_into_bank2(cave)
    total = finish_bank2(b2, rb_id)
    stamp_cave(cave, syms, rb_id, total)

    stock = base.STOCK.read_bytes()
    add_rubber_sites(stock)
    mix.OBJ = BUILD / "rubber_cave.o"
    mix.HERE = BUILD if EMU else HERE
    mix.check_collisions()
    mix.check_alignment()
    symbols = {k: v & ~1 for k, v in syms.items() if base.CAVE_VA <= v < B2_BASE}

    image = bytearray(stock)
    image.extend(b"\x00" * (base.CAVE_OFF - len(image)))
    image.extend(cave)
    print(f"cave {base.CAVE_VA:#x} size {len(cave):#x} ({0x08100000 - base.CAVE_VA - len(cave)} bytes free)")
    print(f"bank 2 payload {len(b2):#x} bytes, id {rb_id:#010x}, sum {total:#010x}")

    for va, symbol, link_, orig in mix.SITES:
        off = va - base.BASE
        if image[off:off + 4].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 4].hex()} != {orig}")
        image[off:off + 4] = base.encode_b(va, symbols[symbol], link_)
    va, orig, dest = base.SKIP_SCREEN
    off = va - base.BASE
    if image[off:off + 2].hex() != orig:
        raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 2].hex()} != {orig}")
    image[off:off + 2] = base.encode_bn(va, dest)
    for va, new, orig in base.TABLES:
        off = va - base.BASE
        if image[off:off + 2].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 2].hex()} != {orig}")
        image[off:off + 2] = bytes.fromhex(new)
    for table, thumb in ((mix.VTABLE, 1), (mix.WORDS, 0)):
        for va, symbol, orig in table:
            off = va - base.BASE
            if struct.unpack_from("<I", image, off)[0] != orig:
                raise SystemExit(f"{va:#x}: word is not {orig:#x}")
            struct.pack_into("<I", image, off, symbols[symbol] | thumb)
    for va, new, orig in mix.BYTES:
        off = va - base.BASE
        if image[off:off + len(new) // 2].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + len(new) // 2].hex()} != {orig}")
        image[off:off + len(new) // 2] = bytes.fromhex(new)
    if image[base.VERSION_OFF] != ord("9"):
        raise SystemExit(f"version byte is {image[base.VERSION_OFF]:#x}, expected '9'")
    image[base.VERSION_OFF] = ord(LETTER)

    mix.verify(image, stock, symbols)
    OUT.write_bytes(image)
    OUT_B2.write_bytes(b2)
    keep = {k: f"{v:#010x}" for k, v in syms.items()
            if (base.CAVE_VA <= v < B2_BASE) or k in ("rubber_boot", "rubber_mix", "rb_header")}
    SYMS.write_text(json.dumps(keep, indent=1) + "\n")
    print("wrote", OUT, len(image), "and", OUT_B2, len(b2))


if __name__ == "__main__":
    sys.exit(main())
