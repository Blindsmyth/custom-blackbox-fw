"""Song DJ row sites (3.1.T only): ../3.1.9-mix-bungee/build_patch.py --lineup adds them.

dj.c is compiled into bank 1 next to deck.c (built with -DWITH_SONG_DJ), dj.S is included by
bungee_cave.S. docs/song-dj.md.
"""


def parts(stock, stock_bl):
    """(sites, vtable): sites are (va, cave symbol, bl?, stock bytes); vtable (slot, symbol, stock pointer)."""
    sites = [
        (0x0805DD34, "hook_dj_step", True, "dbf82040"),                     # Player_Tick: ldr.w r4,[fp,#0x20] (step)
        (0x0805DDDC, "hook_dj_step0", True, stock_bl(stock, 0x0805DDDC, 0x0805CDA0)),  # Player_Tick: the step's events
        (0x080A329E, "dj_song_key", True, stock_bl(stock, 0x080A329E, 0x0809EAEC)),    # SONG button: App_SetScreen(0x2D)
    ]
    vtable = [
        (0x080F1758, "dj_song_tick", 0x080C26FD),     # Song +0x00 tick
        (0x080F1788, "dj_song_child", 0x080C3541),    # Song +0x30 onChildEvent
        (0x080F178C, "dj_song_event", 0x080C3861),    # Song +0x34 onEvent
    ]
    return sites, vtable
