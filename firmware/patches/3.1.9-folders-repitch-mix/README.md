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

- **Hold MIX** to show the second layer. While it's held, tapping a pad selects it without playing it. The MIX press that opens the Mix screen doesn't count: press it again on Mix for the second layer.
- **Blip from rest.** If a param sits at its rest position, a quick touch (under 0.3 s) sets the value under your finger and puts the rest value back on release. Touch longer and the new value stays. Rest positions: Vol at minimum, Cutoff centre, Pitch unshifted, Attack/Decay/Sends at 0. Pan never blips.
- **Centre detent.** Cutoff, Pitch and Pan grow their bar from a white centre line. A drag that passes within a few pixels of the centre sticks at exactly 0.
- **MIX** opens Mix from any screen. On Mix it no longer flips to Mute.
- **INFO** on Mix opens Mute:
  - A short press stays in Mute, and INFO again goes back to Mix (as before).
  - Holding INFO for more than 0.4 s makes Mute momentary: releasing it returns to Mix.

Planned next: [docs/roadmap.md](../../../docs/roadmap.md).

## Other screens

- **Knobs no longer select pads or sequences or switch pages.** On Pads all four knobs are off for now (pad selection and the VEL / CUT-COPY panel switch); on Seq the three pad-selection knobs and the panel switch are off and the top-right knob changes the length (below). Parameter menus keep both knobs: one moves through the list, the other changes the selected parameter.
- **Hold PADS:** the side panel shows CUT / COPY / PSTE / CLR (instead of VEL), and tapping a pad selects it without playing. Let go: VEL comes back.
- **Hold SEQS:** the side panel shows the layers A–D (tap one to pick it) and tapping a pad selects that sequence without starting or stopping it. Let go: OFF / UNDO / CLR come back.
- **Seq length:**
  - The button above UNDO shows the current layer's length in bars ("1 bar", "0.5" …).
  - The top-right knob doubles the length (turn right) or halves it (turn left), at most once per quarter second.
  - Halving drops the notes past the new end; UNDO brings them back.
- **EQ:**
  - Touch a band's dot and drag it straight away; no INFO press to pick the band.
  - New EQs start as Low Shelf, Param, Param, High Shelf.
- **FX button:** toggles between DJ FX and the FX Return page. The Return page has a button row along the bottom, like the pad page's Main / Pos / LFO / Conf: **A Delay | B Reverb | EQ |** (empty). The active return is lit; EQ opens the EQ page. The send page is no longer in the cycle (sends live on Mix).

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
5. Drag Cutoff, Pitch and Pan through the middle: the bar flips sides at the white line, and the value sticks at 0 near it.
6. With Send A at 0, tap its fader quickly: a short send blip, then back to 0. Hold it longer: the send stays.
7. INFO short press stays in Mute; press INFO again to return to Mix. Hold INFO: Mute while held, Mix on release.
8. Save the preset and reload it: the values persist.
9. Folders and Repitch behave as in 3.1.X.
10. Pads: no knob does anything. Seq: only the top-right knob (length). Pad INFO / settings lists: one knob still picks the row, the other changes it.
11. Hold PADS: CUT / COPY / PSTE / CLR replace VEL; tap a pad (selected, silent), tap COPY, tap another pad, PSTE; let go: VEL. Hold SEQS: layers A–D replace OFF/UNDO/CLR; tap a layer, tap a pad (selected, play state unchanged), let go.
12. Seq: the button above UNDO shows the length; turn the top-right knob right (double) and left (halve), then UNDO.
13. EQ: drag each dot directly. A new preset's EQ shows Low Shelf / Param / Param / High Shelf.
14. FX: DJ FX → FX again → Return page with A Delay / B Reverb / EQ / (empty) along the bottom; A and B switch the return, EQ opens the EQ page, FX goes back to DJ FX.
15. Enter Mix with the MIX button: Vol / Decay / Cutoff / Pitch with matching labels. Press and hold MIX again: Pan / Attack / A / B; let go: back. Labels always match the bars.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-folders-repitch-mix/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch-mix/BLACKBOX.BIN
```

`build_patch.py` reuses `../3.1.9-folders-repitch/build_patch.py` (sites, linker, verify). It assembles `mix_cave.S`, which includes the 3.1.X `cave.S` and then `mixui.S`. On top of 3.1.X it patches:
- three `bl App_SetScreen` calls in `App_HandleInput` (MIX at `0x080A32DE`, INFO on Mix at `0x080A30DC`, FX from DJ FX at `0x080A32BE`);
- the scroll-list encoder row select `FUN_080B9BA4` (b.w to a no-op);
- widget vtable words: Mix tick/onChildEvent/onEvent, Pads and Seq onChildEvent/onEvent, EQ graph touchDown, FX Return onChildEvent/onEvent;
- four EQ default band-type bytes.

Sources: `mixui.S` (Mix), `navknobs.S` (Batch 0), `padsel.S`, `seqtools.S`, `eqtouch.S`, `fxret.S` (Batch 1).

The four Mix faders also point at `fader_vt`, a copy of the stock fader vtable with draw, touchDown, touchMove and touchUp wrapped for the centre detent and the blip, so faders on other screens are untouched.
