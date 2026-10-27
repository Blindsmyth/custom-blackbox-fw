"""Song DJ row sites (3.1.T only): ../3.1.9-mix-bungee/build_patch.py --lineup adds them.

dj.c and mixer.c are compiled into bank 1 next to deck.c (built with -DWITH_SONG_DJ), dj.S is
included by bungee_cave.S. docs/song-dj.md.
"""


# (address, new bytes, stock bytes)
BYTES = [
    (0x080EEDF8, "d801", "0000"),          # Tools -> Clock: BPM Project / Global (0x1D8, mixer.c)
]


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
        (0x0809CE82, "mx_bpm_send", True, stock_bl(stock, 0x0809CE82, 0x0804C59C)),   # finalize: BPM / Swing
        (0x0809E258, "mx_load_begin", True, stock_bl(stock, 0x0809E258, 0x0804C764)),  # LoadBank: msg 0x36
    ]
    vtable = [
        (0x080F1758, "dj_song_tick", 0x080C26FD),     # Song +0x00 tick
        (0x080F1788, "dj_song_child", 0x080C3541),    # Song +0x30 onChildEvent
        (0x080F178C, "dj_song_event", 0x080C3861),    # Song +0x34 onEvent
    ]
    return sites, vtable
