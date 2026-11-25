# 3.1.U: Program Change launcher

On the Presets screen, **PSET again** shows four big buttons, 2x2 in the same layout as the encoders.
- **Program:** each encoder turns its corner's program, 1–128.
- **Channel:** after **INFO**, the encoders edit the MIDI channel, 1–16; INFO again switches back.
- **Send:** a tap sends that program change on DIN and USB (the USB-A host jack and the 3.1.UD device, with Tools → USB Dev Out on).
- **Saved:** globally, in `settings.tml` (`pcl1`–`pcl4`).

Details, sites and the bench: [docs/pc-launcher.md](../../../docs/pc-launcher.md).

Files:
- `pcl.c`: bank 1. The PSET hook, the Presets vtable stubs, and the pump in the audio task that sends a press's program change. Also the params and template.
- `pcl_b2.c`: bank 2 (`RbExt.pcl_ui`). The layer itself.
- `pcl.h`: shared state (backup SRAM `0x38800624`, SDRAM buttons).
- `sites.py`: the hook sites.
