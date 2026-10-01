# 3.1.N preset folders

This image is built from stock 3.1.9, and the menu version reads `3.1.N`. New code is appended at `0x080F1E80`, so the file is longer than stock. Stock `firmware/bins/3.1.9/BLACKBOX.bin` and the `3.1.H` test image are unchanged.

Earlier images are superseded. Do not reinstall them:

- 3.1.F to 3.1.L: 3.1.L wrote a debug log from the UI task and damaged a card.
- 3.1.M: kept its state in DTCM, which stock's allocator also hands out, so the preset list stayed empty on the device.

3.1.N has no logger and never writes to the card. It passes the off-device bench (`tools/bench/run_tests.py`, 51 checks) before going near the device. Design and addresses: [docs/preset-folders.md](../../../docs/preset-folders.md).

## What it does

- A directory in `\Presets` that has no `preset.xml` is a group folder. Pressing Load on it stays on the preset screen and lists its contents.
- Inside a group, the list starts with `..`, which goes back up. `..` never goes above `\Presets`.
- A directory that contains `preset.xml` loads as before. A group folder must not have its own `preset.xml`.
- Groups can be nested (`\Presets\Kits\Drums\808\preset.xml`).
- Root `*.xml` legacy presets are only listed at the top level.
- Samples of a loaded preset keep using the folder it was loaded from, even after you browse elsewhere. Save and Save As also write next to the loaded preset.

Known limits:

- After a reboot the browser starts at `\Presets` again.
- A preset that was loaded from inside a group is not restored at boot. Stock behaviour then loads the first loadable row. `settings.tml` stores only the preset name.
- A MIDI program change always uses stock behaviour on the rows currently shown. A group row there falls back to another preset, as in stock.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.N`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## How to test

1. Back up the card first.
2. On the card, make `\Presets\_Test` with no `preset.xml`, and move one or two preset folders into it.
3. Flash this image. Open Presets. `_Test` is listed. Press Load on it.
4. The screen stays on the list and shows `..` plus the moved presets.
5. Load one. It should load and play, including its samples.
6. Open Presets again, choose `..`, and load a top-level preset. It should still play.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-preset-folders/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-preset-folders/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The build script writes `BLACKBOX.BIN`, `BLACKBOX.sym.json` (cave symbols for the bench), and `cave.dis`. It refuses to patch if any site's stock bytes differ.
