"""Song DJ row sites (3.1.T only): ../3.1.9-mix-bungee/build_patch.py --lineup adds them.

dj.c and mixer.c are compiled into bank 1 next to deck.c (built with -DWITH_SONG_DJ), dj.S is
included by bungee_cave.S. docs/song-dj.md.
"""


# (address, new bytes, stock bytes)
BYTES = [
    (0x080EEDF8, "d801", "0000"),          # Tools -> Clock: BPM Project / Global (0x1D8, mixer.c)
    (0x080B2458, "39f90c08", "38f90c08"),  # DJ FX screen: Hold A's second line "A" -> "" (dj.c places it)
    (0x080B245C, "39f90c08", "64ad0c08"),  # DJ FX screen: Hold B's "B" -> "" (the NUL after stock's "A")
    (0x080A30B6, "2521", "2921"),          # INFO on Tools -> Clock: back to Main (Master is in the FX row)
]

# The sample cache: 567 entries instead of stock's 615 (docs/map-319.md "Block cache"). Each entry
# is two 32 KB buffers from the bottom SDRAM allocator at boot, and the sample stack (top down) gets
# what is left: with a big preset loaded that was 190 KB on the unit, and saving one builds its whole
# XML document there (a 350 KB preset.xml peaks at ~1.6 MB), so Save / Save As hung in stock's
# "No more extended memory" loop (0x0808A37C) or wrote no preset.xml. 48 entries = 3 MB more room;
# the cache holds ~97 s instead of 105 s. Every stock bound on the entries is one of these movw.
CACHE_N = 567
_CACHE_COUNT = [0x08071D8E, 0x0807226C, 0x080726DA, 0x08072FDE, 0x08073000, 0x08073024, 0x080731D4,
                0x080733A6, 0x080733BA, 0x0807340E, 0x0807347A, 0x08073902, 0x08074570, 0x08074A42]
_CACHE_END = [0x08070BFA, 0x080723D0, 0x080728E6, 0x08073998]   # mgr + count * 0x1C


def _movw(rd, imm):
    hw1 = 0xF240 | ((imm >> 11) & 1) << 10 | (imm >> 12) & 0xF
    hw2 = ((imm >> 8) & 7) << 12 | rd << 8 | (imm & 0xFF)
    return hw1.to_bytes(2, "little").hex() + hw2.to_bytes(2, "little").hex()


def cache_bytes(stock):
    """BYTES for the smaller cache, checked against the stock movw at each site."""
    out = []
    for sites, old, new in ((_CACHE_COUNT, 0x267, CACHE_N), (_CACHE_END, 0x267 * 0x1C, CACHE_N * 0x1C)):
        for va in sites:
            orig = stock[va - 0x08040000:va - 0x08040000 + 4].hex()
            rd = (int.from_bytes(bytes.fromhex(orig[4:8]), "little") >> 8) & 0xF
            assert orig == _movw(rd, old), f"cache site {va:#x}: {orig}"
            out.append((va, _movw(rd, new), orig))
    return out


def parts(stock, stock_bl):
    """(sites, vtable): sites are (va, cave symbol, bl?, stock bytes); vtable (slot, symbol, stock pointer)."""
    sites = [
        (0x0805DD34, "hook_dj_step", True, "dbf82040"),                     # Player_Tick: ldr.w r4,[fp,#0x20] (step)
        (0x0805DDDC, "hook_dj_step0", True, stock_bl(stock, 0x0805DDDC, 0x0805CDA0)),  # Player_Tick: the step's events
        (0x080A329E, "dj_song_key", True, stock_bl(stock, 0x080A329E, 0x0809EAEC)),    # SONG button: App_SetScreen(0x2D)
    ]
    sites += [                                                              # the mixer (mixer.c)
        (0x0804EE9A, "mx_comp_tap", True, stock_bl(stock, 0x0804EE9A, 0x0804BE40)),   # compressor gain ramp
        (0x0805354A, "mx_bus_tap", True, stock_bl(stock, 0x0805354A, 0x0804D9C0)),    # render: bus 0 L/R
        (0x08053522, "mx_eq_thunk", True, "4146db68"),                      # render: EQ node, then DJ FX
        (0x0809CE82, "mx_bpm_send", True, stock_bl(stock, 0x0809CE82, 0x0804C59C)),   # finalize: BPM / Swing
        (0x0809E258, "mx_load_begin", True, stock_bl(stock, 0x0809E258, 0x0804C764)),  # LoadBank: msg 0x36
        (0x0805353A, "mx_master", True, stock_bl(stock, 0x0805353A, 0x0804D880)),     # render: resample copy; master chain
        (0x08094500, "mc_session_tpl", True, stock_bl(stock, 0x08094500, 0x08093EF6)),  # session template: the chain's values per preset
    ]
    vtable = [
        (0x080F1758, "dj_song_tick", 0x080C26FD),     # Song +0x00 tick
        (0x080F1788, "dj_song_child", 0x080C3541),    # Song +0x30 onChildEvent
        (0x080F178C, "dj_song_event", 0x080C3861),    # Song +0x34 onEvent
        (0x080F0D90, "dj_fx_tick", 0x080AEB45),       # DJ FX +0x00 tick
        (0x080F0DC0, "dj_fx_child", 0x080B1EE5),      # DJ FX +0x30 onChildEvent
        (0x080F0DC4, "dj_fx_event", 0x080B1E9D),      # DJ FX +0x34 onEvent
        (0x080F18A4, "dj_tools_tick", 0x080AEB45),    # Tools +0x00 tick
        (0x080F18D4, "dj_tools_child", 0x080C5361),   # Tools +0x30 onChildEvent (Master: the FX row)
        (0x080F18D8, "dj_tools_event", 0x080C52E1),   # Tools +0x34 onEvent
        (0x080F07BC, "mc_ret_tick", 0x080AEB45),      # FX Return +0x00 tick: CMP Type rebuilds its knobs
    ]
    return sites, vtable
