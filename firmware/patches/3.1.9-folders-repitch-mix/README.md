# 3.1.Q preset folders + Repitch + new Mix

3.1.X (nested preset folders, Clip/Slicer **Warp: Stretch / Repitch**) plus a reworked Mix screen. The menu version reads `3.1.Q`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged. Map: [docs/map-319.md](../../../docs/map-319.md#mix-and-mute).

## What the Mix screen does

- **Pads mute on Mix.** The pads show green (playing) / red (muted). Tap a pad to mute or unmute it; drag across pads to paint the same state. Pads no longer play on Mix.
- **INFO cycles the pad mode:** Mute → Solo → Mute.
  - **Solo:** every pad gets a yellow outline. Tap pads to add them to the solo set: soloed pads show yellow and everything else is muted. Tap them again to take them out; when the set is empty, the mutes you had before soloing come back.
  - A mute edit in Mute mode while a solo is active ends the solo and keeps what you set by hand.
- **Four side faders, two layers.** Each fader shows a param of the selected pad, and the knob in the same corner drives it (stock step and acceleration). You can also drag the faders.

  | | Main layer | | MIX held | |
  | --- | --- | --- | --- | --- |
  | | Left | Right | Left | Right |
  | Top | Vol | Cutoff (Filter) | Pan | Send A (FX1) |
  | Bottom | Decay | Pitch | Attack | Send B (FX2) |

- **Hold MIX** on Mix (momentary):
  - Pan / Attack / Send A / Send B show while it's held.
  - The pads switch to the plain mixer look (no green/red) and tapping one selects it (the faders follow it).
  - Let go: main layer, mute colours, taps mute again.
  - The MIX press that opens the Mix screen doesn't count; Mix always opens on the main layer.
- **Blip from rest.** If a param sits at its rest position, a quick touch (under 0.3 s) sets the value under your finger and puts the rest value back on release. Touch longer and the new value stays. Rest positions: Vol at minimum, Cutoff centre, Pitch unshifted, Attack/Decay/Sends at 0. Pan never blips.
- **Centre detent.** Cutoff, Pitch and Pan grow their bar from a white centre line. A drag that passes within a few pixels of the centre sticks at exactly 0.
- **MIX** opens Mix from any screen. On Mix, holding it shows the second layer (above).

Planned next: [docs/roadmap.md](../../../docs/roadmap.md).

## Other screens

- **Going up a folder:** like the sample browser, the small button at the top left of the preset screen shows **^** inside a folder; tap it (or press BACK) to go up. The ".." row is gone. At the top level the button is blank and BACK works as stock. (Holding BACK still puts the Blackbox into stock standby: display and LEDs off until you press a button.)
- **PSET opens at the top.** The preset list starts on its first row (the first folder) instead of jumping to the loaded preset; the loaded preset is still marked in the list.

- **Knobs no longer select pads or sequences or switch pages.** On Pads all four knobs are off for now (pad selection and the VEL / CUT-COPY panel switch); on Seq the three pad-selection knobs and the panel switch are off and the top-right knob changes the length (below). Parameter menus keep both knobs: one moves through the list, the other changes the selected parameter.
- **"Sel" in the top bar:** while you're selecting (PADS held, SEQS held, or MIX held on Mix), "Sel" shows between the bar counter and "Seq x". "Seq x" sits a little further right to make room.
- **Hold PADS:** the side panel shows CUT / COPY / PSTE / CLR (instead of VEL), and tapping a pad selects it without playing. Let go: VEL comes back.
- **Hold SEQS:** the side panel shows the layers A–D (tap one to pick it) and tapping a pad selects that sequence without starting or stopping it. Let go: OFF / UNDO / CLR come back.
- **Seq length knob:** the top-right knob changes the sequence length, at the same speed as the Step Len list.
  - The list is always Custom, 1/8, 1/4, 1/2, 1 bar … 32 bars, stored as Rec Len. Down from 1/8 is Custom (the length stays); up from an empty Custom sequence is 1/8.
  - On any other length (e.g. 15 steps of 1/16, or a Custom recording) it doubles / halves: ×2 doubles the step count (or, past 512 steps, makes the step 2× longer); ÷2 halves an even count, or makes the step 2× shorter for an odd one (15/16 → 15/32 → 15/64).
  - Going back up after that returns to your step length first: 15/64 → 15/32 → 15/16, then 30/16. Selecting another sequence forgets it.
  - Below the shortest step (15/64 here) the next step down is Custom, as from 1/8.
  - **Dbl Copy** (Tools → Rec, default On): doubling copies the notes into the new half, like stock Double (one UNDO step). Off: it only lengthens.
  - Shortening never deletes notes; they come back when you lengthen again.
  - **Rec Len follows Step Count / Step Len.** Edit either on the settings page and Rec Len switches to the matching length (32 steps of 1/16 = 2 bar), or, for any other length, to an extra last entry that shows the length itself ("15/16", "15/32"). Custom stays the free-length recording mode.
  - The field above UNDO / CLR / OFF shows the length: the Rec Len name ("2 bar", "1/8"), otherwise steps/denominator ("15/16", "63/16"), "Nst" for triplet or bar-length steps or when that doesn't fit ("100st"), and "Cust" for an empty Custom sequence.
- **EQ:**
  - Touch a band's dot and drag it straight away; no INFO press to pick the band.
  - New EQs start as Low Shelf, Param, Param, High Shelf. So does an EQ from an older preset that nobody touched (all bands None, gains 0, frequencies 200/400/600/800) when you open it. An EQ you've edited stays as it is.
- **FX button:** toggles between DJ FX and the FX Return page. The Return page and the EQ page share a button row along the bottom, like the pad page's Main / Pos / LFO / Conf: **A Delay | B Reverb | EQ |** (empty). The page you're on is lit. On the EQ page the EQ view is 40 px shorter to make room. The send page is no longer in the cycle (sends live on Mix).

## Clip / Slicer: ENV source and Env Trig

- **ENV modulation source.** A pad's mod slots (Source list after CC) can now pick **ENV**: the pad's own amp ADSR (Attack / Decay / Sustain / Release). Route it like LFO, e.g. ENV → Cutoff with the slot's Amount for a filter envelope. Each voice follows its own envelope (Clip and Slicer pads have two voices). ENV works on the same targets as LFO (Pitch, Cutoff, Pan, Attack, Decay and a few more).
  - Clip and Slicer pads only for now. On Sample / Multisample pads ENV does nothing yet.
- **Env Trig: Note / Slice** (Clip and Slicer pads, INFO → Conf, the row after Pad Note; scroll down one row):
  - **Note** (default): stock, the envelope starts when the note starts.
  - **Slice:** every time playback crosses into the next slice while the note is held, the envelope (and so ENV) starts again, gliding from where it was so there's no click.
- Both are saved with the preset. Stock firmware ignores them.

## Tools → Master

Stock has one master compressor with a limiter after it. Its settings are hardcoded, and the only control was Tools → Main → Compressor (on/off). Tools now has a fifth section, **Master**:

| Row | Range | Default (= stock) | What it sets |
| --- | --- | --- | --- |
| Compressor | Off / On | as before | Turns the compressor **and** the limiter on (stock behaviour). Moved here from Tools → Main. |
| C Thresh | −48.0 … 0.0 dB | −4.0 | Threshold. The detector is a 128-sample RMS of the master. |
| C Ratio | 1 … 100 (:1) | 4 | Ratio, hard knee. 1 = no compression. |
| C Attack | 0 … 10000 ms | 10 ms | How long the gain takes to fall. |
| C Release | 0 … 10000 ms | 250 ms | How long it takes to come back. |
| C Gain | −36.0 … +36.0 dB | −4.0 | Input gain into the compressor (stock: a fixed −4 dB). Raising it drives the compressor and the limiter harder. |
| L Attack | 0 … 10000 ms | 4 ms | Limiter gain fall time. |
| L Release | 0 … 10000 ms | 750 ms | Limiter recovery time. |

- **Untouched sounds like stock.** The defaults are the stock values.
- **Global, like Compressor.** The values are saved in `settings.tml` as soon as you change them, and stay the same across presets.
- **Limiter limits.** The ceiling stays at 0 dBFS (a literal in stock code). The limiter works per audio block (2.7 ms) without lookahead; the output's hard clip catches short overs.
- **Times of 0 ms** become one sample.
- **Out1–3.** Pads routed there bypass the compressor, as in stock.

## Custom sequence recording

Records MIDI into a sequence the way Ableton records into a new clip.

- **Rec Len** (the field above UNDO on the Seq screen, set with the top-right knob; also on the Seq settings page after MIDI Out): Custom, 1/8, 1/4, 1/2, 1 bar, 2, 4, 8, 16, 32 bars, and the sequence's own length when it's none of these ("15/16"). 1/4 is a quarter note (a quarter of a bar), as in the pad record menu.
  - A fixed value sets the sequence's length. If that length isn't a whole number of steps at the current step length and the layer is empty, the step length switches to 1/16 first.
  - **Custom** changes what REC does, below.
- **Looper take (transport stopped).** Like a Boss loop pedal. It works on any selected sequence whose current layer is empty (any Rec Len, also when it's queued to start with PLAY):
  1. **REC** arms: the REC LED blinks. Nothing runs yet.
  2. **Play.** The first note (pad or MIDI) starts the take and the LED stays on. Play freely, no click.
  3. **REC again** ends the take. The Blackbox picks 1, 2, 4, 8 or 16 bars so the tempo lands in **Tools → Rec → Loop BPM** (60–120, 70–140, 80–160, 90–180, 100–200; default 80–160), sets that tempo, writes your notes quantised (whole steps in step mode, otherwise the Quant Size grid), and starts the transport with the sequence playing from bar 1. One UNDO removes the take.
  - REC before the first note cancels; pressing PLAY while armed cancels too. Takes under 0.25 s are ignored.
  - Example: 8 s of playing with 80–160 → 4 bars at 120 BPM. A take that is too short for the range uses 1 bar at its natural tempo (capped 40–250 BPM).
- **Custom recording (transport running)** works on the same kind of sequence:
  1. **REC** starts recording. The sequence starts playing on its next **Quant Size** boundary (Seq settings, usually 1 bar) and records with no length limit.
  2. **REC again** stops recording, and the loop closes on the next Quant Size boundary. The sequence keeps playing that length. STOP closes it the same way.
  3. A Custom recording can grow to 512 steps (32 bars at 1/16). Past that it loops and overdubs.
- **Tools → Rec → Loop BPM** (60–120 … 100–200, default 80–160): tempo range for looper takes.
- **Tools → Rec → Seq Default** (Custom, 1/8 … 32 bars; default 1 bar) is the Rec Len a sequence gets when it is created, loaded without a Rec Len of its own, or cleared.
  - **CLR always resets** the sequence to Seq Default (Rec Len and length), whether you changed it or not and whether it had notes or not.
- **Step Count** now goes up to 512 (was 256).


## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.Q`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## Check on the device

1. On Mix, tap pads: they turn red (muted) and green again; you hear them drop out. Drag across several pads: they all take the first pad's new state. Pads don't play from Mix any more.
2. Turn each knob, in both layers. The fader in the same corner moves and you hear the change. If a knob moves the wrong fader, the encoder order is off: swap the entries in `knob_slider` in `mixui.S` and rebuild.
3. Drag each fader. The value changes for the selected pad.
4. Hold MIX: Pan / Attack / Send A / Send B appear, and tapping a pad selects it (no mute change). Let go: Vol / Decay / Cutoff / Pitch return. While MIX is held the pads lose their green/red and "Sel" shows in the top bar.
5. Drag Cutoff, Pitch and Pan through the middle: the bar flips sides at the white line, and the value sticks at 0 near it.
6. With Send A at 0, tap its fader quickly: a short send blip, then back to 0. Hold it longer: the send stays.
7. INFO: yellow outlines (Solo). Tap two pads: they turn yellow, everything else red and silent. Tap both again: the earlier mutes come back. INFO again: Mute mode (no outlines).
8. Save the preset and reload it: the values persist.
9. Folders and Repitch behave as in 3.1.X.
10. Pads: no knob does anything. Seq: only the top-right knob (length). Pad INFO / settings lists: one knob still picks the row, the other changes it.
11. Top bar: "Seq x" sits further right. Hold PADS, SEQS, or MIX on Mix: "Sel" appears between the counter and "Seq x"; let go: it disappears. Nothing else in the bar moves or flickers (the label used for Sel was unused by stock; if some screen shows text there, tell me).
11b. Hold PADS: CUT / COPY / PSTE / CLR replace VEL; tap a pad (selected, silent), tap COPY, tap another pad, PSTE; let go: VEL. Hold SEQS: layers A–D replace OFF/UNDO/CLR; tap a layer, tap a pad (selected, play state unchanged), let go.
12. Seq: turn the top-right knob: 1/8 → … → 32bar at Step Len speed (not skipping). A 1-bar pattern turned to 2 bar plays twice (Dbl Copy On); with Tools → Rec → Dbl Copy Off the second bar is empty. Turn below 1/8: Custom. Set Step Count 15: the field shows 15/16; down gives 15/32, 15/64; up gives 15/32, 15/16, 30/16.
13. EQ: drag each dot directly. A new preset's EQ, and an untouched EQ from an older preset, show Low Shelf / Param / Param / High Shelf.
14. FX: DJ FX → FX again → Return page with A Delay / B Reverb / EQ / (empty) along the bottom; A and B switch the return, EQ opens the EQ page, FX goes back to DJ FX.
15. Enter Mix with the MIX button: Vol / Decay / Cutoff / Pitch with matching labels. Press and hold MIX again: Pan / Attack / A / B; let go: back. Labels always match the bars.
16. EQ page: the A Delay / B Reverb / EQ row is along the bottom with EQ lit, the EQ view sits above it, and dots still drag. A and B open that return; FX goes to DJ FX.
17. Seq settings page: set Step Count to 15: Rec Len shows 15/16; Step Len 1/32: Rec Len shows 15/32; Step Count 16 with Step Len 1/16: Rec Len shows 1 bar. Rec Len after MIDI Out. Tools → Rec: Seq Default, Loop BPM, Dbl Copy after Lead In.
18. Looper: load a sound on pad 1, select an empty sequence, transport stopped: press REC. Also on a saved preset that already has sounds, and with MIDI notes. The REC LED blinks. Play a few notes (pads or MIDI) for about 8 seconds, press REC. The LED was steady during the take; now the tempo changes (about 120 with Loop BPM 80–160), the transport starts at once with no lead-in, and the take plays back as a 4-bar loop with notes on the grid. UNDO removes it. Try Loop BPM 60–120 with the same take: 2 bars at about 60 BPM. Arm and press REC again before playing: it cancels.
18b. Transport running, empty Custom sequence: press REC, play 2½ bars, press REC: the loop closes at bar 3 and plays 3 bars back (as before).
19. Turn Rec Len to 2 bar: the sequence is 2 bars long. Save, reload: Rec Len is kept.
20. Tools → Rec → Seq Default = 2 bar. New preset: its sequences are 2 bars. Turn one to 8 bar, CLR: back to 2 bar. CLR an empty sequence set to 4 bar: 2 bar. Seq Default = Custom: CLR gives an empty Custom sequence (shows Cust).
21. Tools shows five tabs: Main, Rec, MIDI, Clock, **Master**. Main no longer has Compressor. Master lists Compressor, C Thresh −4.0, C Ratio 4, C Attack 10 ms, C Release 250 ms, C Gain −4.0, L Attack 4 ms, L Release 750 ms.
22. Compressor On with those defaults sounds as before. Lower C Thresh (e.g. −20.0) with a loud loop: clearly more compression; C Ratio 1: none. Raise C Gain: louder and squashed into the limiter. Power cycle: the values are kept, in every preset.

21. Clip or Slicer pad, INFO → Conf → Env Trig (after Pad Note): Slice. Long Attack, short Decay, low Sustain; hold a note that plays through several slices: each slice swells in again. Env Trig: Note: one swell only.
22. Clip or Slicer pad, mod slot Source ENV → Cutoff, Amount up, Filter low: each note opens and closes the filter with the ADSR (and each slice with Env Trig Slice). Save, reload: kept.

24. PSET: inside a folder the top-left button shows ^ and there is no ".." row; tap ^ (or press BACK once): one level up. At the top the button is blank. BACK on the Pads screen still does stock BACK.
23. PSET: the list opens on the first row (first folder). Scroll down and wait a few seconds: the cursor stays where you put it. Enter a folder: it opens on its first row.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-folders-repitch-mix/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch-mix/BLACKBOX.BIN
```

`build_patch.py` reuses `../3.1.9-folders-repitch/build_patch.py` (sites, linker, verify). It assembles `mix_cave.S`, which includes the 3.1.X `cave.S` and then `mixui.S`. On top of 3.1.X it patches:
- two `bl App_SetScreen` calls in `App_HandleInput` (MIX at `0x080A32DE`, FX from DJ FX at `0x080A32BE`) and the INFO-on-Mix one (`0x080A30DC`), which now cycles the pad mode;
- the Mix view's set-mode call (`0x0808BC40`), its two mute-mode cell fills (`0x080A43E0`, `0x080A43F8`) and touchDown / touchMove vtable words (`mixmute.S`);
- widget vtable words: Mix tick/onChildEvent/onEvent, Pads and Seq onChildEvent/onEvent, EQ graph touchDown, FX Return onChildEvent/onEvent;
- four EQ default band-type bytes;
- Tools → Master (`master.S`):
  - `bl`s at `0x08053CA0` (compressor call → `master_comp`), `0x0808DD3E` (registration, after 0x13B) and `0x08094886` (global template, after 0xB4);
  - two Tools page bounds (`0x080996D2`, `0x08098D5A`, `cmp r6,#3` → `#4`);
  - the Tools → Main list (drop 0xB4) and the empty page-4 list `0x080EEE38`;
  - tab name 4 (`WORDS`: a data pointer to "Master").

`master_comp` reads the seven values from the global bag, clamps them, and writes them into the compressor object (dB as floats, times as ms × 48 samples). Then it runs the stock compressor. Five of the params are stock ids that 3.1.9 registers but never uses: `cthreshdb`, `cratio`, `cattack`, `crelease`, `cgaindb`. Two are new: `lattack` 0x1A8 and `lrelease` 0x1A9. `build_patch.py` stops if two entries patch the same byte.

Sources: `mixui.S` (Mix), `navknobs.S` (Batch 0), `padsel.S`, `seqtools.S`, `eqtouch.S`, `fxret.S` (Batch 1 and the EQ row), `seqrec.S` (Custom sequence recording), `mixmute.S` (Mix pads: mute / solo / select), `adsr.S` (ENV source, Env Trig; with `MIX_EXT` hooks in `../3.1.9-folders-repitch/repitch.S`), `pset.S` (preset list opens on row 0), `looper.S` (looper takes; wraps the note-ring pop `bl` at `0x080A2936`), `master.S` (Tools → Master).

The four Mix faders also point at `fader_vt`, a copy of the stock fader vtable with draw, touchDown, touchMove and touchUp wrapped for the centre detent and the blip, so faders on other screens are untouched.
