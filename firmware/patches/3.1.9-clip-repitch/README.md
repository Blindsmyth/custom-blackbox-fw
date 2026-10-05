# 3.1.W Clip / Slicer Repitch

This image is built from stock 3.1.9, and the menu version reads `3.1.W`. New code is appended at `0x080F1E80`, so the file is longer than stock. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged. This does **not** include the preset-folder patch; keep those as separate images until the audio path is proven on a device.

On a Clip or Slicer pad, open the control panel and the **Pos** tab (scroll list: upper encoder selects the row, lower changes it). **Warp:** is Stretch (default) or Repitch. On Clip it is the fifth row (after Sync). On Slicer it is below the existing seven Pos rows. The value is still the `repitch` attribute on the cell in `preset.xml`. Main stays two rows of four knobs. Sample and Granular lists are untouched.

Map: [docs/map-319.md](../../../docs/map-319.md).

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.W`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## How to test on the device

1. Back up the card first.
2. Set a pad to Clip. Open the control panel, tap **Pos**, and scroll below Sync. The fifth row is **Warp**.
3. **Off (Stretch):** change the global tempo while the clip plays. Pitch stays; length follows tempo (stock).
4. **On (Repitch):** retrigger the clip, then change the global tempo. Pitch should follow tempo. The pad Pitch knob still stacks. A clip that is already playing keeps its old grains until the next launch.
5. Set a pad to Slicer. Pos should show the same **Warp** row under the slicer list. Stretch vs Repitch as above; switching Slicer ↔ Clip must stay up.
6. Tap tempo and MIDI clock while a Repitch clip or slice is playing.
7. Far jumps, reverse, HighQ, scene launch.
8. Sample / Granular pads: no new knob, sound unchanged.
9. Save, reboot, reload: the Repitch setting stays.

If the device disagrees with the map, add a bench or a named fact first, then change the cave. Do not guess in `audioTask`.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-clip-repitch/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-clip-repitch/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The build script writes `BLACKBOX.BIN`, `BLACKBOX.sym.json` (cave symbols for the bench), and `cave.dis`. It refuses to patch if any site's stock bytes differ.

Unicorn cannot run `audioTask`. The bench checks the Pos-list ids, registry title/XML name, and that Sample Pos is unchanged. Sound is device-only.
