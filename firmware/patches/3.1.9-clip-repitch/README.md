# 3.1.W Clip / Slicer Repitch

This image is built from stock 3.1.9, and the menu version reads `3.1.W`. New code is appended at `0x080F1E80`, so the file is longer than stock. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged. This does **not** include the preset-folder patch; keep those as separate images until the audio path is proven on a device.

On a Clip or Slicer pad, open the control panel and the **Pos** tab (scroll list: upper encoder selects the row, lower changes it). **Warp:** is Stretch (default), Repitch, Smooth, Snap, Vari, or Grain. On Clip it is the fifth row (after Sync). On Slicer it is below the existing seven Pos rows (Slicer keeps Stretch/Repitch/Grain on the stock grain path; Smooth/Snap/Vari fall back to stock grains). The value is still the `repitch` attribute on the cell in `preset.xml` (`0` Stretch, `1` Repitch, `2` Smooth, `3` Snap, `4` Vari, `5` Grain). Main stays two rows of four knobs. Sample and Granular lists are untouched.

Smooth is a longer WSOLA stretcher (2048-sample window, hop 1024, correlation search ±160) for one Clip at a time. Snap is Deluge-style: hops guided by the Clip slice map with a short crossfade (run Sync-to-Slice first). Vari builds an epoch/onset map on first play (VariPhrase-style) and places XF hops on it. Grain stays on stock `Clip_StartGrain` but rewrites the hard-coded 256-sample window to 2048 (tempo sync unchanged). Smooth/Snap/Vari share SRAM4 `0x38007000` (one owner); Vari’s map is at `0x3800E800`. Host A/B: `clang -O2 -o tools/stretch/proto tools/stretch/proto.c tools/stretch/smooth.c tools/stretch/snap.c tools/stretch/vari.c -lm` then `--mode smooth|snap|vari|grain|naive`.

Map: [docs/map-319.md](../../../docs/map-319.md). Budget: [docs/memory.md](../../../docs/memory.md).

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.W`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## How to test on the device

1. Back up the card first.
2. Set a pad to Clip. Open the control panel, tap **Pos**, and scroll below Sync. The fifth row is **Warp**.
3. **Stretch:** change the global tempo while the clip plays. Pitch stays; length follows tempo (stock).
4. **Repitch:** retrigger the clip, then change the global tempo. Pitch should follow tempo. The pad Pitch knob still stacks. A clip that is already playing keeps its old grains until the next launch.
5. **Smooth (Clip only):** set Warp to Smooth on **one** Clip, retrigger, change tempo up and down. Pitch stays; length follows tempo with less grain buzz than Stretch. Confirm kicks stay on the grid and a retrigger brings the attack back. A second Smooth/Snap pad must keep stock grains until the first is free. Slicer with Smooth/Snap selected still uses stock grains.
6. **Snap (Clip only):** Sync-to-Slice (or hand slices), set Warp to Snap on the same pad, A/B against Smooth at the same tempo offset. Snap should feel calmer under load; listen for kick integrity vs Smooth’s residual crackle.
7. **Vari (Clip only):** set Warp to Vari on one Clip, retrigger, change tempo. First launch may hitch while the epoch map is built; later plays reuse it until the sample changes. A/B against Smooth/Snap.
8. **Grain:** set Warp to Grain, retrigger, change tempo. Pitch stays; length follows tempo like Stretch, with a longer grain window (less chipmunk buzz, more smear). Works on Clip and Slicer. A/B against Stretch at the same tempo offset.
9. Set a pad to Slicer. Pos should show the same **Warp** row under the slicer list. Stretch / Repitch / Grain as above; switching Slicer ↔ Clip must stay up.
10. Tap tempo and MIDI clock while a Repitch, Smooth, Snap, Vari, or Grain clip is playing.
11. Far jumps, reverse, HighQ, scene launch.
12. Sample / Granular pads: no new knob, sound unchanged.
13. Save, reboot, reload: the Warp setting stays.

If the device disagrees with the map, add a bench or a named fact first, then change the cave. Do not guess in `audioTask`.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-clip-repitch/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-clip-repitch/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The build script writes `BLACKBOX.BIN`, `BLACKBOX.sym.json` (cave symbols for the bench), and `cave.dis`. It refuses to patch if any site's stock bytes differ.

Unicorn cannot run `audioTask`. The bench checks the Pos-list ids, Warp enum labels (Stretch/Repitch/Smooth/Snap/Vari/Grain), registry title/XML name, Smooth/Snap/Vari/Grain flag wiring, Grain window rewrite, and that Sample Pos is unchanged. Sound is device-only.
