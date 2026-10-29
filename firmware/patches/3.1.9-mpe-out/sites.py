"""Seq MIDI out sites (3.1.U): ../3.1.9-mix-bungee/build_patch.py --lineup adds them.

mpe.c is compiled into bank 1 next to dj.c and mixer.c, mpe.S is included by bungee_cave.S
(-DWITH_MPE). docs/mpe-out.md.
"""

# Seq page 0 lists (zero-terminated ids, 0x48 bytes): 0x080EEF28 = 49 46 88 6B (MIDI Out) and
# 0x080EEEE0 = 49 46 88 7A (0xB7 = 2), each with the Mix line's Rec Len (0x19D) after them.
SEQ_LIST = 0x080EEF28
SEQ_LIST_B7 = 0x080EEEE0

# (address, new bytes, stock bytes)
BYTES = [
    # Seq page 0: ... MIDI Out, Rec Len, then Port, MPE, Last Ch, PB Rng.
    (SEQ_LIST + 10, "7400f001f101f2010000", "00000000000000000000"),
    (SEQ_LIST_B7 + 10, "7400f001f101f2010000", "00000000000000000000"),
    # App_SetParam / App_GetParam layer-0 ids 0x6B..0x8A (bit = id - 0x6B): + 0x74 (Port).
    # 0x1F0-0x1F2 go to layer 0 through the Mix line's seqrec_set_layer / seqrec_get_layer.
    (0x080A00CC, "01820020", "01800020"),
    (0x080996AC, "01820020", "01800020"),
]


def parts(stock, stock_bl):
    """Sites (va, cave symbol, bl?, stock bytes)."""
    return [
        (0x0808CDA2, "mpe_register", True, stock_bl(stock, 0x0808CDA2, 0x0808C12C)),  # midiport int registration
        (0x0809441C, "mpe_tpl", True, stock_bl(stock, 0x0809441C, 0x08093EF6)),       # case 0x47: add 0x88
        (0x0805EBCE, "mpe_param_thunk", False, "bdf82420"),   # player param switch: ldrh.w r2,[sp,#0x24]
        (0x0804D004, "mpe_out_thunk", True, stock_bl(stock, 0x0804D004, 0x08044A0E)),  # MIDI out send, 0x50-0x53
    ]
