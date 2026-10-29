#!/usr/bin/env python3
"""Build 3.1.B: 3.1.Q plus Warp Bungee (Bungee Basic in flash bank 2), on 3.1.R's bank-2 machinery.

Outputs, next to this file:
  BLACKBOX.BIN     bank 1 image for the SD installer (stock 3.1.9 + cave)
  BBBUNGEE.BIN     bank 2 payload; the card loader (tools/bank2/b2load.c) programs it on first boot
  BLACKBOX.sym.json, bungee.map

Same structure as ../3.1.9-mix-rubber/build_patch.py (Warp Rubber), with Bungee in place of Rubber Band:
the cave, pairing, card loader (RBTAKE.TXT / RBFREE.TXT), diag, CPU guard and the bank-2 ABI
(tools/stretch/rubber/rubber.h) are shared.

--lineup builds 3.1.T instead (lineup/BLACKBOX.BIN + lineup/BBWARP.BIN): Warp Stutter / Fill /
Repitch / Slice / Grain / Bungee, with Fill, Slice and Grain rendering each lane in bank 2
(tools/stretch/bungee/lanes.cpp). Its own build directory, so it never shares objects with 3.1.B. Needs the Arm GNU Toolchain with newlib/libstdc++
(ARM_GNU, default ~/.local/arm-gnu-15.3/bin), clang for the cave asm, and
tools/stretch/bungee/fetch_bungee.sh run once.
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
sys.path.insert(0, str(ROOT / "tools"))
from version_text import VERSION_REFS, STOCK_VERSION_VA, set_version  # noqa: E402

RUBBER = ROOT / "tools" / "stretch" / "rubber"      # rubber.h (the bank-2 ABI), CMSIS
BUNGEE = ROOT / "tools" / "stretch" / "bungee"
BG = BUNGEE / "bungee"
BANK2 = ROOT / "tools" / "bank2"
DIAG = ROOT / "tools" / "diag"
CMSIS = RUBBER / "cmsis"
BUILD = BUNGEE / "build"
STRETCH = ROOT / "tools" / "stretch"
LINEUP = "--lineup" in sys.argv
if LINEUP:
    BUILD = BUNGEE / "build-lineup"
# The combined 3.1.T build has Clip Deck (../3.1.9-clip-deck: Start/End/Loop, hot cues, the waveform
# editor) too. Its param ids move past the Mix line's (0x19D-0x1A9: Seq Rec, Env Trig, Master).
DECK_DIR = HERE.parent / "3.1.9-clip-deck"
DECK_ID_BASE = 0x1B0
# And the Song DJ row (../3.1.9-song-dj: beatjump and autoloop on the Song screen), which steers Clip
# Deck's units.
DJ_DIR = HERE.parent / "3.1.9-song-dj"
# And Seq MIDI out (../3.1.9-mpe-out: MPE channel spread and Port per sequence layer, docs/mpe-out.md).
MPE_DIR = HERE.parent / "3.1.9-mpe-out"
T = Path(os.environ.get("ARM_GNU", Path.home() / ".local" / "arm-gnu-15.3" / "bin"))
GCC, GXX = T / "arm-none-eabi-gcc", T / "arm-none-eabi-g++"

PAYLOAD = "BBWARP" if LINEUP else "BBBUNGEE"
OUT_DIR = HERE / "lineup" if LINEUP else HERE
OUT = OUT_DIR / "BLACKBOX.BIN"
OUT_B2 = OUT_DIR / f"{PAYLOAD}.BIN"
SYMS = OUT_DIR / "BLACKBOX.sym.json"
ELF = BUILD / "bungee.elf"
if "--emu" in sys.argv:
    OUT, OUT_B2, SYMS = BUILD / "BLACKBOX_emu.BIN", BUILD / f"{PAYLOAD}_emu.BIN", BUILD / "BLACKBOX_emu.sym.json"
    ELF = BUILD / "bungee_emu.elf"
OUT.parent.mkdir(exist_ok=True)
# --with-repitch: Warp Stretch / Repitch / Bungee (Bungee at value 2) instead of Stretch / Bungee.
# Same bank 2 payload; the images go to repitch/ (and build/repitch/ with --emu).
REPITCH = "--with-repitch" in sys.argv
if REPITCH:
    OUT, OUT_B2, SYMS = (OUT.parent / "repitch" / OUT.name, OUT_B2.parent / "repitch" / OUT_B2.name,
                         SYMS.parent / "repitch" / SYMS.name)
    OUT.parent.mkdir(exist_ok=True)
# --mx-safe (with --lineup): the Song DJ mixer without its audio taps (mx_comp_tap, mx_bus_tap, mx_eq_thunk), for
# finding faults on the unit. The BPM setting and the Song screen stay. Images go to lineup/mx-safe/.
MX_SAFE = LINEUP and "--mx-safe" in sys.argv
MX_TAPS = {"mx_comp_tap", "mx_bus_tap", "mx_eq_thunk"}
if MX_SAFE:
    _sub = "mx-safe"
    BUILD = BUNGEE / f"build-lineup-{_sub}"
    ELF = BUILD / "bungee.elf"
    OUT, OUT_B2, SYMS = (OUT.parent / _sub / OUT.name, OUT_B2.parent / _sub / OUT_B2.name,
                         SYMS.parent / _sub / SYMS.name)
    OUT.parent.mkdir(exist_ok=True)
LETTER = "U" if LINEUP else "B"                # 3.1.U: 3.1.T + Seq MIDI out
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
B2_SOURCES = [RUBBER / "rubber.h"] + [BUNGEE / f for f in ("bungee.cpp", "lane.h", "rt.c", "Fourier_cmsis.cpp",
                                                         "bungee-blackbox.patch")]
# 3.1.T's per-lane modes: the Warp Lab's C, built for one voice per lane (beats.h, granular.c).
LANE_SRC = [STRETCH / "beats.c", STRETCH / "granular.c"]
LANE_DEFS = ["-DBEATS_NO_CLOUD", "-DBEATS_SHARED_SCRATCH", "-DBEATS_CACHE=32", "-DGRAN_FW_HANN", "-DGRAN_SRC=256",
             "-DGRAN_NO_TABLES", "-DBEATS_LOOP_GROW=24000"]
if LINEUP:
    B2_SOURCES += [BUNGEE / "lanes.cpp", STRETCH / "beats.h", STRETCH / "granular.h", *LANE_SRC]
BANK1_DEFS = ['-DRB_LABEL="Bungee"', f'-DRB_PAYLOAD_FILE="{PAYLOAD}.BIN"']
BUNGEE_CXX = ["-std=c++20", "-fwrapv", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
              "-DBUNGEE_VISIBILITY=", "-DBUNGEE_SELF_TEST=0", "-DBUNGEE_NO_LOG", "-DBUNGEE_FIXED_PITCH",
              *([] if os.environ.get("BG_EXACT_PHASE") else ["-DBUNGEE_FAST_PHASE"]),
              "-Deigen_assert=BUNGEE_ASSERT1", "-DEIGEN_DONT_PARALLELIZE=1", "-DEIGEN_NO_IO",
              '-DBUNGEE_VERSION="b2"']


def run(cmd):
    subprocess.check_call([str(c) for c in cmd])


def b2_id():
    h = hashlib.sha256()
    for f in B2_SOURCES:
        h.update(f.read_bytes())
    return int(h.hexdigest()[:8], 16)


def compile_all(rb_id):
    if not (BG / "bungee" / "Bungee.h").exists():
        raise SystemExit("Bungee missing: run tools/stretch/bungee/fetch_bungee.sh")
    BUILD.mkdir(exist_ok=True)
    objs = []
    cave_o = BUILD / "bungee_cave.o"
    run(["clang", "-target", "armv7em-none-eabi", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16",
         *(["-DWITH_REPITCH"] if REPITCH else []),
         *(["-DWITH_LINEUP", "-DWITH_DECK", "-DWITH_SONG", f"-DDECK_ID_BASE={DECK_ID_BASE:#x}", "-DWITH_SONG_DJ", "-DWITH_MPE"] if LINEUP else []),
         "-I", MIX_DIR, "-I", mix.BASE_DIR, "-I", DECK_DIR, "-I", DJ_DIR, "-I", MPE_DIR, "-c", "-o", cave_o,
         HERE / "bungee_cave.S"])
    objs.append(cave_o)
    bank1 = [BANK2 / "b2pair.c", BANK2 / "b2load.c", DIAG / "diag.c"] + \
        ([DECK_DIR / "deck.c", DJ_DIR / "dj.c", DJ_DIR / "mixer.c", MPE_DIR / "mpe.c"] if LINEUP else [])
    for src in bank1:
        o = BUILD / (src.stem + ".o")
        run([GCC, *BANK1_C, *BANK1_DEFS, f"-DDECK_ID_BASE={DECK_ID_BASE:#x}", *(["-DSONG_CUES", "-DWITH_SONG_DJ"] if LINEUP else []),
             "-I", BANK2, "-I", RUBBER, "-c", "-o", o, src])
        objs.append(o)
    defs = [f"-DRB_ID={rb_id:#x}u", *(["-DWARP_LANES", "-DWARP_DECK_SEQ", *LANE_DEFS] if LINEUP else [])]
    inc = ["-I", RUBBER, "-I", BG, "-I", BG / "submodules" / "eigen", "-I", CMSIS / "Include", "-I", STRETCH]
    srcs = [f for f in sorted((BG / "src").glob("*.cpp")) if f.name != "Fourier.cpp"]
    for src in srcs + [BUNGEE / "Fourier_cmsis.cpp", BUNGEE / "bungee.cpp"] + ([BUNGEE / "lanes.cpp"] if LINEUP else []):
        o = BUILD / ("bg_" + src.stem + ".o")
        run([GXX, *BANK2_FLAGS, *BUNGEE_CXX, *defs, *inc, "-c", "-o", o, src])
        objs.append(o)
    for src in LANE_SRC if LINEUP else []:
        o = BUILD / ("ln_" + src.stem + ".o")
        run([GCC, *BANK2_FLAGS, *LANE_DEFS, "-I", STRETCH, "-c", "-o", o, src])
        objs.append(o)
    o = BUILD / "rt.o"
    run([GCC, *BANK2_FLAGS, "-c", "-o", o, BUNGEE / "rt.c"])
    objs.append(o)
    for src in CMSIS_SRC:
        o = BUILD / ("cmsis_" + src.stem + ".o")
        run([GCC, *BANK2_FLAGS, "-DARM_MATH_CM7", "-D__FPU_PRESENT=1", "-I", CMSIS / "Include",
             "-c", "-o", o, src])
        objs.append(o)
    return objs


def bank1_has_no_ram(objs):
    """Cave code has nowhere to keep .data/.bss: they would land in bank 2's SRAM1 block."""
    for o in objs[:8 if LINEUP else 4]:          # the cave asm, then the bank-1 C (compile_all)
        out = subprocess.check_output([str(T / "arm-none-eabi-size"), "-A", str(o)], text=True)
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith((".data", ".bss")) and int(parts[1]) > 0:
                raise SystemExit(f"{o.name}: {parts[0]} is {parts[1]} bytes; bank 1 code cannot have RAM")


def link(objs):
    run([GXX, *BANK2_FLAGS, "-nostartfiles", "-T", HERE / "bungee.ld", "-Wl,--gc-sections",
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
         "-j", ".ARM.exidx", "-j", ".itcm", "-j", ".b2data", ELF, b2])
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


# 3.1.Y's 2048 slices (slice.S) keep their tables in SRAM1+2, where bank 2's .data, heap and
# stacks live, so the leaf builds leave them out (cave.S skips slice.S under WARP_RUBBER).
SLICE_SITES = {"hook_slices_max", "hook_slice_max", "hook_engine_cap", "hook_cell_cap", "hook_save_iter",
               "hook_save_bag", "hook_cell_ext", "hook_pad_slice", "hook_pad_done"}


def deck_parts():
    """Clip Deck's own sites, vtable slot and tables (its build lists them on top of 3.1.Z's), with
    the param ids in the Clip Pos lists moved to DECK_ID_BASE."""
    spec = importlib.util.spec_from_file_location("deck_build", DECK_DIR / "build_patch.py")
    deck = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(deck)
    shared = {va for va, *_ in base.SITES}
    sites = [s for s in deck.SITES if s[0] not in shared and s[1] not in SLICE_SITES]
    ids = (DECK_ID_BASE + 4, DECK_ID_BASE + 5, DECK_ID_BASE + 14) + tuple(range(DECK_ID_BASE + 6, DECK_ID_BASE + 14))
    tables = [(at + 2 * i, struct.pack("<H", pid).hex(), "0000")
              for at in (0x080EF99A, 0x080EF292) for i, pid in enumerate(ids)]
    tables += [(0x080A5594, "0922", "0c22"), (0x080A55A0, "1580", "1880")]   # 9-char name label (deck)
    return sites, deck.VTABLE, tables


# 3.1.T Song cues (docs/song-cues.md; deck.c song_*, deck.S hook_song_step, hook_pad_start).
SONG_SITES = [
    (0x0809450C, "song_template", True, "fff7f3fc"),     # Param_BagTemplate case 0x4E (scene): Play Count add
    (0x0805CDA0, "hook_song_step", False, "90f85810"),   # FUN_0805CDA0 entry (b.w): ldrb.w r1,[r0,#0x58]
    (0x080C323A, "song_panel", True, "f5f732f8"),        # Song screen ctor: A-D / Keep panel layout
]
SONG_VTABLE = [
    # Song screen vt+0x30 / +0x34 (song_event, song_event2) are installed by the Song DJ row (dj_song_child,
    # dj_song_event), which hands on to them.
    (0x0806823C, "hook_pad_start", 0x080683B5),          # Clip pad event switch, msg 0x8B (scene pad event)
]


def add_rubber_sites(stock):
    mix.SITES[:] = [site for site in mix.SITES if site[1] not in SLICE_SITES]
    if LINEUP:
        sites, vtable, tables = deck_parts()
        # 0x08066CF2 (next grain) is the Mix line's env_slice site too: hook_wrap_grain jumps on to it.
        mix.SITES[:] = [site for site in mix.SITES if not (site[0] == 0x08066CF2 and site[1] == "env_slice")]
        mix.SITES.extend(sites)
        mix.VTABLE.extend(vtable)
        base.TABLES.extend(tables)
        spec = importlib.util.spec_from_file_location("dj_sites", DJ_DIR / "sites.py")
        dj = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(dj)
        sites, vtable = dj.parts(stock, stock_bl)
        if MX_SAFE:
            sites = [s for s in sites if s[1] not in MX_TAPS]
        mix.SITES.extend(sites)
        mix.VTABLE.extend(vtable)
        mix.BYTES.extend(dj.BYTES)
        # Master moves from the Tools tabs to the FX row (fxret.S, dj.c): Tools keeps its 4 tabs.
        mix.WORDS[:] = [w for w in mix.WORDS if w[0] != 0x080EEE90]
        mix.SITES.extend(SONG_SITES)
        mix.VTABLE.extend(SONG_VTABLE)
        spec = importlib.util.spec_from_file_location("mpe_sites", MPE_DIR / "sites.py")
        mpe = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mpe)
        mix.SITES.extend(mpe.parts(stock, stock_bl))
        mix.BYTES.extend(mpe.BYTES)
    if not any(site[1] == "hook_lane" for site in mix.SITES):   # 3.1.S and later list it themselves
        mix.SITES.append((0x080667B8, "hook_lane", False, "2de9f04f"))   # per-lane render: the leaf renders
    mix.SITES.extend([
        (0x080B10E0, "warp_count", True, stock_bl(stock, 0x080B10E0, 0x0808E2A4)),  # Warp list per pad
        (0x08071472, "sd_read_retry", True, stock_bl(stock, 0x08071472, 0x0807714C)),  # sample block read
        *[(va, "b2load_thunk", True, stock_bl(stock, va, 0x08092F5C))
          for va in (0x0807443E, 0x0807446E, 0x080744D8)],     # pcmStreamer file steps: card loader
        (0x080414E4, "cpu_wait", True, stock_bl(stock, 0x080414E4, 0x08088756)),   # audio wait: CPU meter
        (0x080C5264, "tools_thunk", True, stock_bl(stock, 0x080C5264, 0x0809BBF8)),  # Tools bar: load / crash
    ])
    # The leaf task (b2pair.c rb_task) needs ~0.7 KB of FreeRTOS heap, which stock leaves ~0.35 KB
    # of (heap_4, 36 KB, all stacks): the idle task's stack 0x400 -> 0x300 words (it runs no hook).
    mix.BYTES.append((0x08088C98, "4ff44072", "4ff48062"))   # vTaskStartScheduler: mov.w r2,#0x300
    mix.VTABLE.extend([                                           # crash recorder (tools/diag/diag.c)
        (0x0804000C, "fault_handler", 0x08046267),                # HardFault
        (0x08040010, "fault_handler", 0x08046269),                # MemManage
        (0x08040014, "fault_handler", 0x0804626B),                # BusFault
        (0x08040018, "fault_handler", 0x0804626D),                # UsageFault
        (0x08040040, "wdg_handler", 0x080402AB),                  # IRQ 0 WWDG1: early wakeup
    ])


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
    mix.OBJ = BUILD / "bungee_cave.o"
    mix.HERE = OUT.parent
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
    if LINEUP:
        # "3.1.T" after the cave and the version display repointed at it: stock's '9' stays, as the
        # naming keyboard's 9 key shares it (tools/version_text.py, as Clip Deck does).
        set_version(image, LETTER)
        version_va = struct.unpack_from("<I", image, VERSION_REFS[0] - base.BASE)[0]
        symbols["__version"] = version_va
        mix.WORDS.extend((ref, "__version", STOCK_VERSION_VA) for ref in VERSION_REFS)
    else:
        if image[base.VERSION_OFF] != ord("9"):
            raise SystemExit(f"version byte is {image[base.VERSION_OFF]:#x}, expected '9'")
        image[base.VERSION_OFF] = ord(LETTER)

    mix.verify(image, stock, symbols)
    OUT.write_bytes(image)
    OUT_B2.write_bytes(b2)
    keep = {k: f"{v:#010x}" for k, v in syms.items()
            if (base.CAVE_VA <= v < B2_BASE) or k in ("rubber_boot", "rubber_mix", "rb_header", "rb_fast_used",
                                                      "rb_fast_peak", "rb_itcm_used", "rb_heap_failed", "__b2_bss_end", "__itcm_start", "__itcm_end")}
    SYMS.write_text(json.dumps(keep, indent=1) + "\n")
    print("wrote", OUT, len(image), "and", OUT_B2, len(b2))


if __name__ == "__main__":
    sys.exit(main())
