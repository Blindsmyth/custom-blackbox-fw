"""Show our build's version on screen without touching stock's "3.1.9".

Stock's "3.1.9" (0x080CF290) ends in a '9' that the linker pooled: 0x080CF294 is also the standalone "9" the
naming keyboard and two numeric lists point at (pool words 0x080EAFC4, 0x080EB834, 0x080F0BA8). Overwriting that
byte with the build letter made the keyboard's 9 key type the letter. Instead, the version text goes after the cave
and the four pool words that display the version are repointed at it. (Found by blackbox-mod, patches/cave.py.)
"""

import struct

BASE = 0x08040000
STOCK_VERSION_VA = 0x080CF290
VERSION_REFS = (0x0809AF30, 0x080AEAF4, 0x080C55E8, 0x080C72C8)   # literal-pool words holding STOCK_VERSION_VA
BANK1_END = 0x08100000                                               # the installer refuses anything past this


def set_version(image, letter):
    """Append "3.1.<letter>" to `image` and point the version display at it. Returns the stock offsets changed."""
    off = STOCK_VERSION_VA - BASE
    if bytes(image[off:off + 6]) != b"3.1.9\0":
        raise SystemExit(f"stock version text is {bytes(image[off:off + 6])!r}, expected b'3.1.9\\0'")
    image.extend(b"\0" * (-len(image) % 4))
    va = BASE + len(image)
    image.extend(f"3.1.{letter}".encode() + b"\0")
    image.extend(b"\0" * (-len(image) % 4))
    if BASE + len(image) > BANK1_END:
        raise SystemExit(f"image ends at {BASE + len(image):#x}, past bank 1")
    changed = set()
    for ref in VERSION_REFS:
        o = ref - BASE
        if struct.unpack_from("<I", image, o)[0] != STOCK_VERSION_VA:
            raise SystemExit(f"{ref:#x} does not hold the stock version pointer")
        struct.pack_into("<I", image, o, va)
        changed.update(range(o, o + 4))
    print(f"  version 3.1.{letter} at {va:#010x}, {len(VERSION_REFS)} pool words repointed")
    return changed
