"""Program Change launcher sites (3.1.U): ../3.1.9-mix-bungee/build_patch.py --lineup adds them.

pcl.c is compiled into bank 1, pcl_b2.c into bank 2 (RbExt.pcl_ui). docs/pc-launcher.md.
"""

VTABLE_EVENT = 0x080F14CC     # Presets vtable +0x34: the Mix line's pset_on_event; pcl_event goes in front of it


def parts(stock, stock_bl):
    """(sites, vtable): sites (va, cave symbol, bl?, stock bytes); vtable (slot, symbol, stock pointer)."""
    sites = [
        (0x080A32EA, "pcl_pset_key", True, stock_bl(stock, 0x080A32EA, 0x0809EAEC)),   # PSET: App_SetScreen(7)
        (0x0804CED8, "pcl_pump", True, stock_bl(stock, 0x0804CED8, 0x08044AD8)),       # MIDI out pump (audio task)
    ]
    vtable = [
        (0x080F1498, "pcl_tick", 0x080BE595),     # Presets +0x00 tick
        (0x080F14C8, "pcl_child", 0x080BD929),    # Presets +0x30 onChildEvent
    ]
    return sites, vtable
