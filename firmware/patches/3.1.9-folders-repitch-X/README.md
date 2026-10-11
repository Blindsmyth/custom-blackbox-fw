# 3.1.X preset folders + Repitch

Prebuilt image: stock 3.1.9 with nested preset folders and Clip/Slicer **Warp: Stretch / Repitch**. No Smooth, Snap, Vari, Grain or 2048 slices. The menu version reads `3.1.X`.

Built from the `repitch` branch (commit `6f90987`, `firmware/patches/3.1.9-folders-repitch/build_patch.py`) and bench-tested there (173 passed). The source lives on that branch, not in this folder.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.X`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.
