#!/usr/bin/env python3
"""Build the 3.1.Q image: 3.1.X (preset folders + Warp Stretch/Repitch) plus the Mix overhaul.

Reuses ../3.1.9-folders-repitch/build_patch.py (sites, linker, verify) and adds the Mix hooks
(two App_HandleInput call sites, three Mix vtable words) and the Batch 0 encoder hooks
(Pads/Seq onEvent words). Does not modify the
stock file. Writes BLACKBOX.BIN, BLACKBOX.sym.json and cave.dis here.
"""

import importlib.util
import json
import struct
import subprocess
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

HERE = Path(__file__).resolve().parent
BASE_DIR = HERE.parent / "3.1.9-folders-repitch"
spec = importlib.util.spec_from_file_location("folders_repitch_build", BASE_DIR / "build_patch.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

CAVE_S = HERE / "mix_cave.S"
OBJ = HERE / "cave.o"
OUT = HERE / "BLACKBOX.BIN"
SYMS = HERE / "BLACKBOX.sym.json"
LETTER = "Q"
MAX_CAVE = 0x4000

# (site, cave symbol, bl?, stock bytes): both replace `bl App_SetScreen` in App_HandleInput.
SITES = base.SITES + [
    (0x080A32DE, "mix_btn_screen", True, "fbf705fc"),  # MIX button: Mix -> Mute toggle
    (0x080A30DC, "mix_info", True, "fbf706fd"),        # INFO on Mix: next pad mode (Mute/Solo)
    (0x0808BC40, "mix_set_mode", True, "2af000f9"),    # GUI shows Mix/Mute: always mute colours
    (0x080A43E0, "mix_mute_fill", True, "eaf71ffb"),   # cell mute-mode fill (on): Solo colours
    (0x080A43F8, "mix_mute_fill", True, "eaf713fb"),   # cell mute-mode fill (off)
    (0x080A32BE, "fx_to_return", True, "fbf715fc"),    # FX button: DJ FX -> send page
    (0x0808BF20, "eq_page_ctor", True, "1ef0b6f9"),     # GUI ctor -> EQ page ctor
    (0x080AA8F8, "eq_graph_rect", True, "fff7bcfb"),    # EQ layout -> graph setRect
    # Custom sequence recording (seqrec.S)
    (0x0808C420, "seqrec_register", True, "fff75afe"),  # Param_RegisterAll: enum 0x2D
    (0x080949D2, "seqrec_global_tpl", True, "fff790fa"),  # global bag template, last add
    (0x08094426, "seqrec_seq_tpl", True, "fff766fd"),   # sequence bag template, last add
    (0x0809FE98, "seqrec_set_layer", False, "092bcfd8"),  # App_SetParam layer-0 ids
    (0x08099672, "seqrec_get_layer", False, "092be8d8"),  # App_GetParam layer-0 ids
    (0x080A1BE6, "seqrec_setparam", True, "fdf733ff"),  # App_HandleUiEvent 0x6E -> App_SetParam
    (0x080A1C8A, "seqrec_clear", True, "fdf7c9fd"),     # CLR
    (0x080A7F20, "sr_labels_hook", True, "e6f7aaf9"),   # settings row build: Param_Labels
    (0x080A1C92, "seqrec_clear_all", True, "f6f7d3fb"),  # clear all layers
    (0x080A2F3C, "seqrec_rec_press", True, "fef7c2fb"),  # REC press
    (0x080AB594, "hdr_tick", True, "00f0faf8"),       # header bar tick (direct call): "Sel"
    (0x080A2936, "looper_pop", True, "a9f70dff"),       # App_Update note-ring pop (looper)
    (0x080BDE96, "pset_no_current", True, "d9f717fe"),  # preset screen 0x68: no jump to the loaded preset
    (0x080BDFEE, "pset_no_current", True, "d9f76bfd"),  # preset list rebuilt: open on row 0
    # Clip/Slicer ENV source and slice-triggered envelopes (adsr.S)
    (0x080685C4, "env_route", True, "fef728fd"),       # mod routing FUN_08067018
    (0x08066B9A, "env_slice", True, "fef7d9fc"),       # slice change: FUN_08065550 (site 1)
    (0x08066CF2, "env_slice", True, "fef72dfc"),       # slice change: FUN_08065550 (site 2)
    (0x080A282E, "seqrec_recstate", True, "f8f717fa"),  # recording on/off
    (0x080A2696, "seqrec_started", True, "faf7d3fd"),   # sequence started
    # Tools -> Master (master.S)
    (0x08053CA0, "master_comp", True, "fbf79cf8"),     # master render: bl FUN_0804EDDC (compressor)
    (0x0808DD3E, "master_register", True, "fef7f5f9"),  # Param_RegisterAll: Param_RegisterInt(0x13B)
    (0x08094886, "master_tpl", True, "fff736fb"),      # global bag template: add(0xB4)
]

# Mix/Mute widget vtable 0x080F0F10: (address, cave symbol, stock word)
VTABLE = [
    (0x080F0F10, "mix_tick", 0x080B6601),
    (0x080F0F20, "mix_touch_down", 0x080B5F45),        # pads: select / mute / solo
    (0x080F0F24, "mix_touch_move", 0x080B5EBD),
    (0x080F0F40, "mix_child_event", 0x080B5A4D),
    (0x080F0F44, "mix_on_event", 0x080B5C71),
    (0x080F0E84, "pads_on_event", 0x080B4135),         # Pads onEvent
    (0x080F0E80, "pads_child_event", 0x080B3E85),      # Pads onChildEvent
    (0x080F0E50, "pads_tick", 0x080B476D),             # Pads tick (edit panel while PADS held)
    (0x080F0EC4, "seq_on_event", 0x080B5285),          # Seq onEvent
    (0x080F0EC0, "seq_child_event", 0x080B4DED),       # Seq onChildEvent
    (0x080F0E90, "seq_tick", 0x080B55F5),              # Seq tick (layer panel while SEQS held)
    (0x080F066C, "eq_touch_down", 0x080A9B41),         # EQ graph touchDown
    (0x080F07F0, "fxret_on_event", 0x080AB901),        # FX Return page onEvent
    (0x080F07EC, "fxret_child_event", 0x080AB9B5),     # FX Return page onChildEvent
    (0x080F06EC, "eq_child_event", 0x080AA24D),        # EQ page onChildEvent
]

# (address, new bytes, stock bytes): EQ band types for new slots, `movs r2, #type` in the
# group defaults (0x08093F20 case 0x36). L Shelf 2, Param 3, Param 3, H Shelf 4.
BYTES = [
    (0x0808CE70, "4ff40073", "4ff48073"),               # 0x86 Step Count max 256 -> 512
    (0x080EEF30, "9d01", "0000"),                       # Seq page 0: Rec Len
    (0x080EEEE8, "9d01", "0000"),                       # Seq page 0 (MIDI display): Rec Len
    (0x080EED66, "9e01", "0000"),                       # Tools -> Rec: Seq Default
    (0x080EED68, "a001", "0000"),                       # Tools -> Rec: Loop BPM
    (0x080EED6A, "a101", "0000"),                       # Tools -> Rec: Dbl Copy
    (0x080EF910, "a201", "0000"),                       # Slicer Conf: Env Trig after Pad Note
    (0x080EF208, "a201", "0000"),                       # Slicer Conf (type-0x4C list copy): Env Trig
    (0x080EFA78, "a201", "0000"),                       # Clip Conf: Env Trig (clips play through slices too)
    (0x080EF370, "a201", "0000"),                       # Clip Conf (type-0x4C list copy): Env Trig
    (0x0808CB3E, "09210091", "cdf800b0"),               # mod source count 8 -> 9 (movs r1,#9; str r1,[sp])
    (0x080B67CC, "092b", "082b"),                       # knob page loop: cmp r3,#8 -> #9
    (0x080B7994, "0920", "0820"),                       # mod screen loop: movne r0,#8 -> #9
    (0x0809470E, "0222", "0022"),
    (0x0809474E, "0322", "0022"),
    (0x0809478E, "0322", "0022"),
    (0x080947CE, "0422", "0022"),
    # Tools -> Master (master.S): a fifth Tools page
    (0x080996D2, "042e", "032e"),                       # page model, Tools: cmp r6,#3 -> #4
    (0x08098D5A, "042e", "032e"),                       # Tools row builder: cmp r6,#3 -> #4
    (0x080EED28, "9b010000", "b4009b01"),               # Tools -> Main: drop Compressor (0xB4)
    # Tools -> Master (page 4, empty in stock): Compressor, Thresh, Ratio, Attack, Release,
    # Gain, L Attack, L Release
    (0x080EEE38, "b40031013201" "2f0130013a01" "a801a9010000",
     "000000000000" "000000000000" "000000000000"),
]

# (address, cave symbol, stock word): data pointers into the cave, no Thumb bit.
WORDS = [
    (0x080EEE90, "s_master", 0x080CB060),              # Tools tab names[4]: "" -> "Master"
    # Mod source list: 8 stock entries + ENV (adsr.S); readers as in j3threejay/blackbox-mod
    (0x0808CCC0, "env_src_names", 0x080ECD0C),         # registration: names
    (0x080B6964, "env_src_names", 0x080ECD0C),         # knob page: names
    (0x0809A69C, "env_src_codes", 0x080ECCFC),         # app: list index -> code
    (0x080B696C, "env_src_codes", 0x080ECCFC),         # knob page: code -> index
    (0x080B7B00, "env_src_codes", 0x080ECCFC),         # mod screen: code -> index
]


# Word tables in the cave. Stock code reads some with LDM, which hard-faults on the M7 when
# unaligned (Unicorn doesn't fault, so the bench can't catch it): keep them 4-byte aligned.
WORD_TABLES = ["fxr_bar_rect", "fxr_labels", "fader_vt", "rest_values", "layer_main",
               "layer_held", "ticks_per_beat", "sr_short_labels", "sr_regs", "sr_len_labels",
               "sr_set_labels", "sr_len_ticks", "sr_960", "sr_bpm_labels",
               "sr_dbl_labels", "sr_len_ticks_q", "lp_k", "m_regs", "m_defaults", "m_fields",
               "env_targets", "env_src_names", "env_src_codes", "sr_envtrig_labels"]


def check_collisions():
    """Every patched byte belongs to exactly one SITES / VTABLE / WORDS / BYTES / base entry."""
    owner = {}
    spans = ([(va, 4, f"site {sym}") for va, sym, *_ in SITES]
             + [(va, 4, f"word {sym}") for va, sym, _ in VTABLE + WORDS]
             + [(va, len(new) // 2, f"bytes {va:#x}") for va, new, _ in BYTES]
             + [(va, 2, f"table {va:#x}") for va, *_ in base.TABLES]
             + [(base.SKIP_SCREEN[0], 2, "skip screen")])
    for va, n, what in spans:
        for a in range(va, va + n):
            if a in owner:
                raise SystemExit(f"collision at {a:#x}: {what} and {owner[a]}")
            owner[a] = what


def check_alignment():
    out = subprocess.check_output(["arm-none-eabi-nm", str(OBJ)], text=True)
    addr = {parts[2]: int(parts[0], 16) for parts in (l.split() for l in out.splitlines()) if len(parts) == 3}
    bad = [f"{n} at {addr[n]:#x}" for n in WORD_TABLES if addr.get(n, 0) & 3]
    missing = [n for n in WORD_TABLES if n not in addr]
    if bad or missing:
        raise SystemExit(f"word tables not 4-byte aligned: {bad} missing: {missing}")


def main():
    check_collisions()
    subprocess.check_call(["clang", "-target", "armv7em-none-eabi", "-mthumb",
                           "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16", "-I", str(BASE_DIR),
                           "-c", "-o", str(OBJ), str(CAVE_S)], cwd=str(HERE))
    check_alignment()
    blob, symbols = base.load_cave(OBJ)
    if len(blob) > MAX_CAVE:
        raise SystemExit(f"cave is unexpectedly large: {len(blob):#x}")
    stock = base.STOCK.read_bytes()
    image = bytearray(stock)
    if len(image) > base.CAVE_OFF:
        raise SystemExit("stock image overlaps the cave")
    image.extend(b"\x00" * (base.CAVE_OFF - len(image)))
    image.extend(blob)
    print("cave", hex(base.CAVE_VA), "size", hex(len(blob)))

    for va, symbol, link, orig in SITES:
        off = va - base.BASE
        if image[off:off + 4].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + 4].hex()} != {orig}")
        image[off:off + 4] = base.encode_b(va, symbols[symbol], link)
        print(f"  {'bl' if link else 'b.w':4} {va:#010x} -> {symbol}")
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
    for table, thumb in ((VTABLE, 1), (WORDS, 0)):
        for va, symbol, orig in table:
            off = va - base.BASE
            if struct.unpack_from("<I", image, off)[0] != orig:
                raise SystemExit(f"{va:#x}: word is not {orig:#x}")
            struct.pack_into("<I", image, off, symbols[symbol] | thumb)
            print(f"  u32  {va:#010x} -> {symbol}")

    for va, new, orig in BYTES:
        off = va - base.BASE
        if image[off:off + len(new) // 2].hex() != orig:
            raise SystemExit(f"{va:#x}: stock bytes {image[off:off + len(new) // 2].hex()} != {orig}")
        image[off:off + len(new) // 2] = bytes.fromhex(new)
        print(f"  b{len(new) // 2}   {va:#010x} -> {new}")

    if image[base.VERSION_OFF] != ord("9"):
        raise SystemExit(f"version byte is {image[base.VERSION_OFF]:#x}, expected '9'")
    image[base.VERSION_OFF] = ord(LETTER)

    verify(image, stock, symbols)
    OUT.write_bytes(image)
    SYMS.write_text(json.dumps({k: f"{v:#010x}" for k, v in symbols.items()}, indent=1) + "\n")
    print("wrote", OUT, "size", len(image))


def verify(image, stock, symbols):
    B = base.BASE
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    insns = list(md.disasm(bytes(image[base.CAVE_OFF:]), base.CAVE_VA))
    (HERE / "cave.dis").write_text(
        "\n".join(f"{i.address:#010x}: {i.mnemonic:8} {i.op_str}" for i in insns) + "\n")
    for va, symbol, link, _ in SITES:
        insn = next(md.disasm(bytes(image[va - B: va - B + 4]), va))
        if insn.mnemonic not in ("bl", "b.w", "b") or int(insn.op_str.lstrip("#"), 16) != symbols[symbol]:
            raise SystemExit(f"{va:#x} decoded as {insn.mnemonic} {insn.op_str}")
    for table, thumb in ((VTABLE, 1), (WORDS, 0)):
        for va, symbol, _ in table:
            if struct.unpack_from("<I", image, va - B)[0] != symbols[symbol] | thumb:
                raise SystemExit(f"{va:#x}: word not {symbol}")
    allowed = {base.VERSION_OFF}
    for va, *_ in SITES:
        allowed.update(range(va - B, va - B + 4))
    for va, *_ in VTABLE + WORDS:
        allowed.update(range(va - B, va - B + 4))
    for va, new, _ in BYTES:
        if image[va - B: va - B + len(new) // 2].hex() != new:
            raise SystemExit(f"{va:#x}: patched bytes wrong")
        allowed.update(range(va - B, va - B + len(new) // 2))
    allowed.update(range(base.SKIP_SCREEN[0] - B, base.SKIP_SCREEN[0] - B + 2))
    for va, *_ in base.TABLES:
        allowed.update(range(va - B, va - B + 2))
    extra = [i for i, (a, b) in enumerate(zip(stock, image)) if a != b and i not in allowed]
    if extra:
        raise SystemExit(f"unexpected diffs in the stock part: {[hex(i) for i in extra[:12]]}")
    print("verify ok")


if __name__ == "__main__":
    main()
