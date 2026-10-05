# 3.1.X preset folders + Warp

This image is stock 3.1.9 plus both the nested-preset-folder patch (`3.1.V`) and Clip/Slicer Warp (`3.1.W`). The menu version reads `3.1.X`. New code is appended at `0x080F1E80`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged.

Folder state stays in SRAM4 at `0x38000000`. Warp flags sit at `0x38005000`, past the folder block, so a list rebuild cannot wipe them.

## What it does

Everything in [../3.1.9-preset-folders/README.md](../3.1.9-preset-folders/README.md): group folders, `..` / BACK, Save As into the browsed folder.

And everything in [../3.1.9-clip-repitch/README.md](../3.1.9-clip-repitch/README.md): on a Clip or Slicer pad, **Pos** has **Warp:** Stretch or Repitch. `repitch` in `preset.xml` is ignored by stock 3.1.9.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.X`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-folders-repitch/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The bench runs the folder cases and the Warp cases on this image.
