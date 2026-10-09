# 3.1.Q preset folders + Repitch + new Mix

3.1.X (nested preset folders, Clip/Slicer **Warp: Stretch / Repitch**) plus a reworked Mix screen. The menu version reads `3.1.Q`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged. Map: [docs/map-319.md](../../../docs/map-319.md#mix-and-mute).

## What the Mix screen does

- **Pads play on Mix.** Tapping a pad triggers it exactly like on the Pads screen, so it also records with REC. Playing a pad selects it.
- **Four side faders, two layers.** Each fader shows a param of the selected pad, and the knob in the same corner drives it (stock step and acceleration). You can also drag the faders.

  | | Main layer | | MIX held | |
  | --- | --- | --- | --- | --- |
  | | Left | Right | Left | Right |
  | Top | Vol | Cutoff (Filter) | Pan | Send A (FX1) |
  | Bottom | Decay | Pitch | Attack | Send B (FX2) |

- **Hold MIX** to show the second layer. While it's held, tapping a pad selects it without playing it.
- **Blip from rest.** If a param sits at its rest position, a quick touch (under 0.3 s) sets the value under your finger and puts the rest value back on release. Touch longer and the new value stays. Rest positions: Vol at minimum, Cutoff centre, Pitch unshifted, Attack/Decay/Sends at 0. Pan never blips.
- **MIX** opens Mix from any screen. On Mix it no longer flips to Mute.
- **INFO** on Mix opens Mute:
  - A short press stays in Mute, and INFO again goes back to Mix (as before).
  - Holding INFO for more than 0.4 s makes Mute momentary: releasing it returns to Mix.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.Q`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## Check on the device

1. On Mix, tap pads: they play. With REC armed they record.
2. Turn each knob, in both layers. The fader in the same corner moves and you hear the change. If a knob moves the wrong fader, the encoder order is off: swap the entries in `knob_slider` in `mixui.S` and rebuild.
3. Drag each fader. The value changes for the selected pad.
4. Hold MIX: Pan / Attack / Send A / Send B appear, and tapping a pad selects it silently. Let go: Vol / Decay / Cutoff / Pitch return.
5. With Send A at 0, tap its fader quickly: a short send blip, then back to 0. Hold it longer: the send stays.
6. INFO short press stays in Mute; press INFO again to return to Mix. Hold INFO: Mute while held, Mix on release.
7. Save the preset and reload it: the values persist.
8. Folders and Repitch behave as in 3.1.X.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-folders-repitch-mix/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch-mix/BLACKBOX.BIN
```

`build_patch.py` reuses `../3.1.9-folders-repitch/build_patch.py` (sites, linker, verify). It assembles `mix_cave.S`, which includes the 3.1.X `cave.S` and then `mixui.S`. On top of 3.1.X it patches:
- two `bl App_SetScreen` calls in `App_HandleInput` (MIX at `0x080A32DE`, INFO on Mix at `0x080A30DC`);
- three words of the Mix vtable (tick, onChildEvent, onEvent).

The four Mix faders also point at `fader_vt`, a copy of the stock fader vtable with touchDown/touchUp wrapped for the blip, so faders on other screens are untouched.
