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
MAX_CAVE = 0x3000

# (site, cave symbol, bl?, stock bytes): both replace `bl App_SetScreen` in App_HandleInput.
SITES = base.SITES + [
    (0x080A32DE, "mix_btn_screen", True, "fbf705fc"),  # MIX button: Mix -> Mute toggle
    (0x080A30DC, "info_to_mute", True, "fbf706fd"),    # INFO on Mix -> Mute
    (0x080A32BE, "fx_to_return", True, "fbf715fc"),    # FX button: DJ FX -> send page
]

# Mix/Mute widget vtable 0x080F0F10: (address, cave symbol, stock word)
VTABLE = [
    (0x080F0F10, "mix_tick", 0x080B6601),
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
]

# (address, new bytes, stock bytes): EQ band types for new slots, `movs r2, #type` in the
# group defaults (0x08093F20 case 0x36). L Shelf 2, Param 3, Param 3, H Shelf 4.
BYTES = [
    (0x0809470E, "0222", "0022"),
    (0x0809474E, "0322", "0022"),
    (0x0809478E, "0322", "0022"),
    (0x080947CE, "0422", "0022"),
]


def main():
    subprocess.check_call(["clang", "-target", "armv7em-none-eabi", "-mthumb",
                           "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16", "-I", str(BASE_DIR),
                           "-c", "-o", str(OBJ), str(CAVE_S)], cwd=str(HERE))
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
    for va, symbol, orig in VTABLE:
        off = va - base.BASE
        if struct.unpack_from("<I", image, off)[0] != orig:
            raise SystemExit(f"{va:#x}: vtable word is not {orig:#x}")
        struct.pack_into("<I", image, off, symbols[symbol] | 1)
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
    for va, symbol, _ in VTABLE:
        if struct.unpack_from("<I", image, va - B)[0] != symbols[symbol] | 1:
            raise SystemExit(f"{va:#x}: vtable word not {symbol}")
    allowed = {base.VERSION_OFF}
    for va, *_ in SITES:
        allowed.update(range(va - B, va - B + 4))
    for va, *_ in VTABLE:
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
