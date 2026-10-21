# 3.1.E Clip Start / End / Loop, MIDI hot cues and the Clip editor

This image is 3.1.Z (preset folders, Clip/Slicer Warp, 2048 slices; see [../3.1.9-folders-repitch/README.md](../3.1.9-folders-repitch/README.md)) plus an Ableton-style region on Clip pads: a play start, a play end, and a loop inside them. The menu version reads `3.1.E`. New code is appended at `0x080F1E80`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged.

It also has phase 2 of the Clip overhaul: 8 DJ-style hot cues per Clip pad, played from MIDI notes. [docs/clip-deck.md](../../../docs/clip-deck.md) has the later phases (beat jump, master loop roll).

Preset folder fixes of 2026-10-21 (no dropout entering big folders, marks kept for the last 8 folders, safe Save As in deep folders, errors shown on a failed save) are in this image too; see [../3.1.9-folders-repitch/README.md](../3.1.9-folders-repitch/README.md#folder-fixes-2026-10-21).

## What it does

Open a Clip pad's waveform page. Stock leaves the four encoders empty on Clip; here they are (the main layer, no popup open):

| Encoder | What | Steps |
| --- | --- | --- |
| Top left | **Str** play start | by the Grid size |
| Top right | **End** play end (`end` = end of the file) | by the Grid size |
| Bottom left | **Grid** size: 1/16 … 16 bars, default 1 bar, never finer than the clip's Sync | one size |
| Bottom right | – | – |

The knob titles show the value as `bar.beat.16th` (1-based), e.g. `Str 17.1.1`. Loop Start / Loop End are in the Loop popup (see [Clip editor](#clip-editor-phase-3)).

- **Loop.** Loop on / off is **On** in the Loop popup. It is the stock Clip **Loop Mode** (`loopmode`).
  - Loop on: the clip plays from Start into the loop, then repeats Loop Start → Loop End.
  - Loop off: it plays Start → End once.
- **Bottom bar.** Clip pads use the Slicer bottom bar with its `...` button. The sample name next to it is cut to 9 characters, as on Slicer.
- **Defaults.** Loop Start and Loop End follow Start and End, so a stock clip still loops as a whole.
- **Units.** Positions are stored in **1/16 notes**. They move by the **Grid** size, which can't be finer than the clip's **Sync** (Sync 1/4 → Grid ≥ 1/4). The audio cuts at Sync units, which is why the Grid stops there. Markers are drawn where the audio actually cuts.
- **When it does nothing.** With Sync **None** the stock engine plays the whole file as one grain, and with Sync **Slice** the units are slices. In both cases the region does nothing and the clip plays like stock.
- **Pos list.** The Clip **Pos** list gets Cue Mode, Cue Note, Grid and **Cue A … Cue H** after Warp. Cues are set there in **beats** (`bar.beat`, one list step = one beat) or **Off**. Start, End and Loop are edited on the waveform page.
- **Warp.** Stretch, Repitch and Grain follow the region. Smooth, Capi and Vari keep their own read head, so while a region is set they fall back to stock grains. With no region set they work as before.
- **XML.** Attributes on the cell: `cstart16`, `cend16`, `cloopst16`, `cloopend16`, `cueb1`–`cueb8`, `cgrid`, `cgridofs`. Stock 3.1.9 ignores them. End and Loop End are `65535` when untouched, which means the end of the file. Saves from the earlier 3.1.E builds (beats, `clipstart` …) are not read: those builds were test only.
- **Long files.** Positions reach 16383 beats; End and Loop End at the end follow the file however long it is.

## Hot cues (phase 2)

Each Clip pad has 8 cue points. With **Cue Mode** on, MIDI notes **Cue Note** … Cue Note + 7 jump to them, DJ style: the clip keeps playing and jumps at the next **Quant Size** boundary, so it stays in time.

| Param | Id | XML | Values |
| --- | --- | --- | --- |
| Cue Mode | `0x1A1` | `cuemode` | Off / On (default Off) |
| Cue Note | `0x1A2` | `cuenote` | MIDI note of Cue A, shown with stock Pad Note names (default 36 = `C2`) |
| Cue A … Cue H | `0x1A3`–`0x1AA` | `cueb1`–`cueb8` | 0 = Off, k = beat k − 1 (shown `bar.beat`). Cue A defaults to beat 0 (the start). |

- **Where.** Cue Mode, Cue Note and Cue A–H are in the Clip **Pos** list; the cues are also set and played in the Cue popup, and show as flags on the waveform.
- **Which notes.** The note must reach the pad with its note number. That is only true on the pad's own **MIDI In** channel (Conf page, `midimode`). Global In triggers (default map and Pad Note) carry note 0 and never fire a cue. Set the Clip pad's MIDI In to a channel and send the cue notes on it.
- **Playing clip.** A cue note sets a pending jump and does not retrigger. The first grain that starts on a Quant Size boundary plays from the cue instead. With Quant Size None it jumps at the next Sync unit. A later cue note before the boundary replaces the pending one. An unset cue (`-`) does nothing.
- **Gapless jumps.** Pads stream from the card, so on a long stem the cue's audio may not be in memory. The cue note already asks pcmStreamer for the cue's audio (three 8192-frame blocks, about 0.5 s at 48 kHz) and asks again on every grain until the jump, so it is cached when the jump plays. With Quant Size None the lead time is one Sync unit.
- **Stopped clip.** A set cue launches the clip at the cue (stock launch quantize, note 0 like a pad press). An unset cue launches it like any other note.
- **Units.** Cues are 1/16 notes and play from the Sync unit they fall in. With Sync None or Slice, and on Slicer pads, notes behave like stock.
- **Region.** A jump replaces the next grain, so it wins over a loop wrap at the same boundary. After jumping past Loop End the clip plays on to End and then loops. That also applies when Loop End is turned down behind the playhead (P1 wrapped at once).
- **Warp.** Smooth, Capi and Vari fall back to stock grains while Cue Mode is on, like with a region.
- **Values.** A cue is stored as 0 = unset, k = 1/16 note k − 1 (that is what `preset.xml` holds). The engine table in SRAM4 keeps 1/16 notes with `0xFFFF` for unset.

## Clip editor (phase 3)

The bottom bar's **`...`** opens a row in place of the overview strip, like on Slicer, with three tabs: **Cue | Loop | Grid**. A tab opens a popup in the middle of the screen, at the place of Slicer's Scan / Grid box (`{20, 50, 280, 40}`). Tap the tab again, or `...`, to close it; `...` with no popup closes the row.

| Popup | Buttons | Top left | Top right | Bottom left | Bottom right |
| --- | --- | --- | --- | --- | --- |
| none | – | Start | End | Grid size | zoom the waveform (`Zoom 4 bars`: the screen's width) |
| **Cue** | **A B C D E F G H** (lit = set) | select a cue (`Cue C`) | move it by the Grid in whole beats (`Pos 33.1.1`); turned left past the start it clears the cue (`Pos Off`) | Grid size | swap it with its neighbour |
| **Loop** | **On**, **Instant**, **Nudge** | Loop Start by the Grid | Loop End by the Grid | loop size ×2 / ÷2 (1/16 … 64 bars); halving keeps the half the playhead is in | move the loop by the Grid |
| **Grid** | **Lines**, **< 1**, **1 >**, **Raw** | nudge the grid (1 ms steps) | – | Grid size | zoom |

- **Cue letters.** Lit = set (an "on" toggle). Tap a lit letter: it plays that cue (same note path as MIDI: a playing clip jumps at the next Quant Size boundary, a stopped one starts there). Tap a dark letter: it stores the position of the **white marker** in the middle of the waveform there, to the nearest beat, and turns Cue Mode on. Cue A is set to the start by default.
- **Cues on.** A Clip pad uses its cues (Keys layout, MIDI cue notes) when Cue Mode is on **or** any cue is set. With Cue A set by default, every new Clip pad opens the Keys screen on its cue keys, with set cues lit.
- **Flags.** Set cues show as flags on the waveform (stock's slice-marker style); the selected one is highlighted. Grid lines (on by default) use the same markers.
- **Clear.** Select the cue (top left) and turn the position knob (top right) left past the start: it reads `Pos Off` and the letter goes dark. Or set it to Off in the Pos list.
- **Clear / set by holding** (needs two touches at once): hold the **Cue** tab and tap a lit letter: it is cleared. Tap a dark letter: the marker position goes there. Overwrite = tap it twice (clear, then set). Letting go of Cue after this does not close the popup.
- **Loop buttons.** On = Loop Mode. Instant = a loop starting at the bar line nearest the white marker (quantized like rekordbox / Traktor), keeping the current length (1 bar if the loop is the whole region), Loop Mode on. Nudge = the loop one Grid step later.
- **Grid buttons.** Lines = grid lines on the waveform, on by default: one per beat, or per Grid step when the Grid is finer than a beat, thinned to every 2nd/4th/… so they stay ≥ 10 px apart (not saved; on again after a reboot). `< 1` / `1 >` move the grid one Grid step against the audio. Raw = every encoder turn counts 8× (lit); dark = Fine.
- **Loop moves take the playhead along.** With Loop on, once the playhead is in the loop it stays there whatever moves the loop (Loop Start / End, Move, Nudge, Instant, the Pos list): a unit the loop no longer covers is taken to the same place in the loop, before or after it. Moving a playing loop one bar later moves the playhead one bar later (Traktor / rekordbox loop move). A note start or a cue jump may land outside the loop: from before it the clip plays into the loop, from past it the clip plays on to End and then loops.
- **Zoom.** Bottom right on the main layer and in the Grid popup: √2 per step, around the white marker (the same target stock's pinch sets, `wave+0x24C`).
- **Play from the cursor.** With the `...` row open on a Clip pad, starting the pad (pad, MIDI, sequencer) starts it at the start of the bar the white marker is in (not before Start). With the row closed, or on another screen, it starts as usual.
- **Grid lines live.** Lines and flags are rebuilt on every draw of the waveform (its draw slot `0x080F1968` → `deck_wave_draw`), for the part on screen, so they follow scrolling, zoom and playback.
- **Grid offset.** Nudge and `< 1` / `1 >` set `cgridofs`, in samples. The whole unit grid moves by it: every grain starts that much later (or earlier) in the file, so the bar lines, Start/End, the loop and the cues all sit on the first downbeat of a file that does not start on one.

A swap moves the selected cue's time to its neighbour's letter and follows it, so turning keeps moving the same point along.

- **Keys screen.** A Clip pad with Cue Mode on shows stock's Slicer key layout as 8 keys labelled **A–H**, with **CUES** on the root button. The keys play Cue 1–8 whatever Cue Note is. Its STOP button does nothing on such a pad.
- **Beat Count from the file name.** With Beat Count on Auto, a file name with a tempo (`Bass 128bpm.wav`, `Loop_174.5_BPM.wav`, `BPM 90 drums.wav`, 40–300) sets the clip length to round(seconds × BPM / 60) beats instead of stock's power-of-two guess. Only the file name counts, not its folders. An explicit Beat Count and stock's ACID import win. The param still reads Auto.

## How it works

Stock Clip plays one grain per Sync unit: `clip+0x1FC` samples per unit, `clip+0x208` ticks per unit, 960 ticks per beat. It wraps when the next unit reaches `clip+0x240 + 0x244` (`FUN_080667b8`).
- **Note start (`0x08065A9A`).** The first unit becomes Start (`deck_note_unit`).
- **Unit limit (`0x08066C46`, Sync branch).** The limit becomes Loop End when looping, and End otherwise (`deck_limit`). Stock then stops, or re-syncs and wraps to unit 0.
- **Next grain (`0x08066CF2`).** A wrap to unit 0 becomes Loop Start (`deck_wrap_unit`).
- **`Clip_RecalcRates` (via `hook_rate`).** With a region, the wrap re-syncs to one unit (`+0x230 = +0x208`), not to the clip length or Quant Size: Loop Start rarely sits on that grid. It also publishes the unit size for the screen.
- **Grain hooks.** Both `Clip_StartGrain` hooks save registers to SRAM and never push, so its three stack arguments stay where they are. This is the same rule as `hook_grain`.
- **Waveform screen.** The hooks are on the Clip refresh branch (`0x080A583C`, knob binding), the Clip update case (`0x080A5B0E`, markers and knob ranges), the bottom bar (`0x080A5568`) and the event handler entry (`0x080A66DC`, Loop press). Knob edits reuse the stock knob-change post (UI event `0x6E`), so the bag, the engine message and `preset.xml` all go through stock code.
- **State.** SRAM4 `0x38004930`–`0x38005000` (`DkState`, 0x688 bytes, magic `DEC4`): header and stub register saves, the region table (16 pads × `DkPad`), the cue table (`DkCue`, 24 bytes, with the playhead unit), the grid table (`DkGrid`: offset, size), the editor state (`DkUi`), up to 64 grid markers, and three stock buttons for the row. The block sits between the folder state and the Warp flags. It is free on this line and on the Bungee / Mix line, whose own SRAM4 state starts at `0x38005110` ([docs/memory.md](../../../docs/memory.md)). The clip object has no free bytes.
- **Editor.** The row is stock's edit row (`view+0x2F8`), already the head of the view's widget list, so its children draw on top and get touches first. It holds 12 buttons: its own four, its five spares (`FUN_080C4FF0`, ids `0x1C0`–`0x1C4`) and three stock buttons built in SRAM4 (`FUN_080A3D34`, ids `0x1C5`–`0x1C7`), added once per view. Three are the tabs, eight the popup items, and the last appended is the popup's background box (drawn first, hit last). Popup buttons sit in Scan's box although they belong to the row: children are drawn and hit-tested without clipping to the parent. "Hold Cue" is the Cue tab's touch count (`+0x98`). Presses go through `deck_ui_event` (`0x080A66DC`), encoder turns through `hook_knob` at `0x080A6E70` (the only `FUN_080B0D54` call) to `deck_knob_turn`. All four knobs are blank stock knobs with our titles. All edits post the stock knob-change event (`0x6E`), so bag, engine and `preset.xml` stay stock.
- **Grid offset.** `hook_grain_ofs` at `0x080656E2` in `Clip_StartGrain` (`vmov fp, s15`, the grain's first frame) adds the pad's offset (`deck_grain_start`, clamped to the file); the code there keeps 1 in `lr` for a later store, so the stub returns with `lr = 1`. The cue prefetch and the waveform's markers, span and loop add it too.
- **Keys.** `0x080AE036` (pad selected) forces the Slicer layout for a Clip in Cue Mode; `0x080AD9CA` sets 8 keys, `0x080AD768` the letters, `0x080AD99C` the CUES label; `0x080A0AA2` / `0x0809B78C` map keys 36–43 to Cue Note + 0–7 and drop STOP.
- **Beat Count up to 1024.** Stock registers Beat Count (`0xD2`, `beatcount`) as a list of 513 labels: Auto, 1…512. `deck_reg_beats` (at `0x0808D708` in `Param_RegisterAll`) registers it as a number 0…1024 instead. The values are the same (0 = Auto, else beats), so `preset.xml` and the engine are unchanged. The engine has no cap of its own (`clip+0xB80`, beats × 960 ticks). The Pos list prints 0 as `Auto`. What stock 3.1.9 does with a preset saved above 512 beats is not checked (its list has no label there).
- **Beat Count Auto from the name.** `0x08064BA2` in `Clip_RecalcRates` (`ldr r3, [r4, #0xB80]`) returns the file-name beats on Auto (`deck_auto_beats`, path at the wave slot `+0x5C`).
- **Hot cues.** MIDI path, filtering and the ids are in [docs/map-319.md](../../../docs/map-319.md#midi-note-path-pad-trigger). The three voice starts in `FUN_08067FA4` (`0x080683AE`, `0x08068656`, `0x080686CC`) go through `hook_voice` → `deck_cue_note`, which passes the note on, rewrites it to 0 for a cue launch, or drops the call and sets the pending unit. `FUN_08065908` takes two stack arguments, so the stub saves registers to SRAM and never pushes. The next-grain stub passes the lane render's grain tick (`[sp+0x58]`) to `deck_wrap_unit`, which takes the jump when that tick is a multiple of `clip+0x218` (stock `__aeabi_ldivmod`). `hook_limit` now also passes the next unit (`r8`) to `deck_limit`.
- **Cue prefetch.** `deck_cue_note` calls the sample ref's seek (`FUN_0806375C(clip+0x24, unit × spu − 8)`, the call `Clip_StartGrain` makes), and `deck_wrap_unit` repeats it on every grain while a jump is pending. The seek has no read head. It refreshes the cache age of the target blocks or queues them for pcmStreamer (`FUN_08074554`), so the playing grain is untouched and the engine never touches FatFs. Repeats are skipped while 32 or more requests are waiting in pcmStreamer's 128-entry ring. Details: [docs/map-319.md](../../../docs/map-319.md#clip-sample-length).

Code: [deck.c](deck.c) (logic), [deck.S](deck.S) (stubs), and the region calls chained into the Warp hooks in [repitch.S](repitch.S).

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.E`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## How to test on the device

Earlier builds have run on hardware. Bugs found there, fixed since but not yet re-tested:
- **Grid knob looked stuck, Grid defaulted to 1/16.** After a param change the app updates the screen with force 0, and stock then skips the Clip part where the titles were refreshed; they are now refreshed on every update. A pad saved before Grid existed has no Grid value: it now reads 1 bar.
- **Pos-list cues stuck at 0.** Their range was 0–65535; the list knob cannot step through that many values. Cues are now beats, 0–4096 (Off, then `bar.beat`).
- **Grid stuck, cues not lit / not cleared / not saved, Pos-list cues stuck.** Root cause: a stock param list holds 64 rows (`FUN_08093E6A`, capacity at `+0xC`), and a Clip template with our ids already filled them up to `0x1A0`. `Param_Add` (`FUN_08093EF6`) drops rows past the capacity without a word, so Cue Mode, Cue Note, the cues, Grid and the grid offset (`0x1A1`–`0x1AC`) were never in the pad's list. Edits reached the engine only through the knob event, the screen read 0, and `preset.xml` did not get them. `hook_bag_size` (`0x08093E7C`) now gives lists asked for 64 rows 96 (SDRAM bump heap, +256 B per list).
- **Loop escaped when its markers moved.** With Loop on, a unit at or past Loop End now wraps back into the loop, at the same place in it (`in_loop` in `deck_wrap_unit` / `deck_note_unit`). That covers Loop End, Loop Start, Move, Nudge and Pos-list edits. A cue jump past the loop still plays on to End, then loops. Halving still keeps the half the playhead is in.
- **Grid lines only after turning the Grid knob, and only near one spot.** They were set on stock's screen update, for the part shown then. They are now rebuilt on every draw.
- **Moving the loop still let the playhead escape** (a loop moved past the playhead). A playhead in the loop now moves with it.
- **Hold Cue + letter did not clear.** It overwrote first and cleared on a second tap. Now one tap on a lit letter clears it.
- **Knob titles one layer behind** (the main layer blank after `...`, Cue showing Str/End, …). A stock knob shows a copy of its title (`knob+0xF4`) that is refreshed by `FUN_080B0480`; we changed only the source text (`knob+0x75`). Titles now refresh at once; the bench reads the shown copy.
- **Cues landing at odd places.** They were stored at the last grain the engine started. They now go to the white marker.
- **Clearing a cue did nothing.** The hold-Cue + tap gesture needs two touches; there is now a one-finger way (position knob past the start, or `-` in the Pos list).
- **`...` / Loop button drawn as a black rectangle.** Stock's waveform update hides the bar's `...` button on every pad that is not a Slicer (`0x080A54F6`: `view+0x1D18` = mode ≠ 2). The black box was the empty bar behind it. `deck_bar` now shows it again on Clip after every update; the bench runs stock's whole update and presses `...` through the real handler. (The first guess, a 12-character name running over the button, was wrong. The name is still cut to 9 characters as on Slicer: `0x080A5594`, `0x080A55A0`.)
- **Audio stopped and only part of the waveform was drawn on long files.** End and Loop End at 512 were read as beat 512. They now mean the end of the file (`end_units`).

The bench covers the engine math and the stub register/stack contracts, not sound or drawing.

1. Back up the card first.
2. Load a long stem on a Clip pad with Sync 1/4. Open the waveform page.
   - **Corners.** Top left reads `Str 1.1.1`, top right `End end`, bottom left `Grid 1 bar`, bottom right empty. Turn each and check it changes the title in its own corner.
3. Turn **Str** 4 steps and **End** down to `End 13.1.1` (Grid 1 bar: one step = one bar).
   - The start and end markers move by whole bars.
   - The playhead starts at bar 5 on the next launch and loops bars 5–13 in time.
4. Open `...` → **Loop**. Turn **LpS** to `7.1.1` and **LpE** to `8.1.1`.
   - Launch: the clip plays bar 5 → 8, then loops bar 7 → 8.
   - Kicks stay on the grid. Listen for a click at the loop seam (stock crossfade is 256 samples).
5. In the Loop popup press **On**.
   - It goes dark and the loop markers vanish.
   - The clip plays bar 5 → 13 once and stops.
   - Press again to bring the loop back.
6. **Grid vs Sync.** Set Grid to 1/4 and move Start by single beats. Set Sync to **1 bar**: Grid jumps up to 1 bar and won't go below it. Set Sync to **None**: the markers go and the clip plays whole.
7. Warp: try Repitch and Grain with a region. Then set Smooth: with a region it should sound like Stretch; with the region back to full it should sound like Smooth.
8. **Bottom bar.**
   - Switch the pad Clip → Slicer: the button reads `...` and still opens Grid/Scan/Join/Split.
   - Switch to Sample: the stock Str/Len/LpStr/LpEnd knobs still work.
   - On a Clip pad whose sample has a long name (12+ characters), the bar shows 9 characters and then a readable `...` button.
9. **Long file.** Load a stem longer than 4:30 on a fresh Clip pad, Sync 1/4, Loop off.
   - The whole waveform is drawn and it plays to the end of the file.
   - Turn End down a few bars: it stops there. Turn it back up to `End end`: it plays to the end again.
   - Turn Start past bar 129 (beat 512, the old limit): it works.
10. Save, reboot, reload: the region and loop state stay. Load that preset on stock 3.1.9: it plays the whole clip.
11. Name a preset with a 9 in it; the 9 key should type `9`.

### Hot cues

Use a MIDI keyboard or controller that can send notes on a chosen channel.

12. On a Clip pad with a long stem and Sync 1/4, open INFO → **Conf** and set **MIDI In** to Ch 1. Open **Pos**: after Warp it reads Cue Mode, Cue Note (`C2`) and Grid (`1 bar`).
13. Set Grid to 1/4 on the waveform page. Open `...` → **Cue**. Let the clip play and tap **A** at beat 1, **B** at beat 17, **C** at beat 33 (or close: they snap down to the beat). Stop the clip and the transport.
    - Send note 37 (C#2): the clip starts at Cue B and is in time.
    - Stop it. Send note 36: it starts at Cue A. Send note 39 (Cue D, unset) on a stopped clip: it starts like a pad press.
14. Quant Size **1 bar**, transport running, clip playing. Send note 38 in the middle of a bar.
    - It keeps playing until the next bar line, then jumps to Cue C. No retrigger click at the note.
    - Send 37 and then 36 within one bar: only the last one (Cue A) happens.
    - Send 39 (unset): nothing happens.
15. Quant Size **None**: the jump comes on the next beat (next Sync unit). Quant Size **4 bars**: it waits for the 4-bar line.
16. **Launch modes.** Repeat 14 with Launch Mode Trigger, Gate and Toggle.
    - Gate: releasing a cue note should not stop the clip (it was started by another note). Write down what happens.
    - Toggle: cue notes jump and never stop the clip (a cue launch stores note 0, so stock's same-note stop does not match). A pad press or a note outside the cue range toggles as stock.
17. **Channels.** Send the cue notes on another channel, or rely on Global In: nothing jumps (Global In triggers carry note 0). Notes outside 36–43 on Ch 1 retrigger the clip like stock.
18. **Region.** Start bar 1, End bar 17, loop bars 5–7, Loop on. While it loops, send note 38 (Cue C, past the loop): it jumps at the bar, plays on to End, then loops bars 5–7 again.
19. **Warp.** With Cue Mode on, Smooth falls back to stock grains (it sounds like Stretch). Turn Cue Mode off: Smooth sounds like Smooth again.
20. **Slicer / Sync None.** Switch the pad to Slicer, or Sync to None: notes 36–43 do what they do on stock.
21. **Far jumps on a long stem.** Load a stem of 6 minutes or more (Beat Count set to its real length, or from a `…bpm` file name) with Sync 1/4. Set Cue A near the start and Cue B near the end, Quant Size 1 bar.
    - Play from Cue A, then trigger Cue B. The jump comes on the bar with no silence or click at the start.
    - Jump back and forth a few times, including right after loading the preset, before anything late in the file has played.
    - Repeat with Quant Size None (one beat of lead time) and with three long stems playing at once.
    - Any gap means a block was still not cached: note the Quant Size and how many pads were playing.
22. Save, reboot, reload: Cue Mode, Cue Note, the cues, Grid and the grid offset stay (open `preset.xml`: `cueb1`…, `cgrid`, `cgridofs` are on the pad). Load that preset on stock 3.1.9: it plays and ignores the new attributes.

### Clip editor

23. **Row.** On a Clip pad press `...`: the row reads **Cue | Loop | Grid**. Press `...` again: it closes. Switch to Slicer: Split | Join | Scan | Grid, and Grid / Scan still open their popups.
24. **Popup place.** Tap **Cue**: A–H appear in a box at the same place as Slicer's Scan box. Tap **Loop**, then **Grid**: their buttons appear there instead. Tap the lit tab again or `...`: the popup closes, the row stays. Check that the waveform does not paint over the box while the clip plays.
25. **Cue letters.** Scroll the waveform so the white marker sits on a kick, tap a dark letter: it lights, a flag appears on the marker (Sync 1/4: on the beat), Cue Mode turns on. Do it for A, B, C. Tap a lit letter: the clip jumps there at the next Quant Size boundary. Check the Pos list: Cue A–C read `bar.beat.16th`.
26. **Clear.** Select a set cue (top left), turn top right to the left past the start: `Pos Off`, the letter goes dark and its flag goes. Set a cue to Off in the Pos list: same. In the Pos list, set Cue E to a beat: it lights in the popup and a flag appears. Then hold **Cue** with one finger and tap a lit letter with another: it goes dark (cleared). Tap it again, still holding: set at the marker. If that two-finger gesture does nothing, the screen does not report two touches at once here.
27. **Titles.** Press `...`: Str / End / Grid show at once. Tap Cue, Loop, Grid in turn: each shows its own titles straight away (Cue A / Pos / Grid / Swap, LpS / LpE / Size / Move, Nudge / – / Grid / –). Top left selects a cue (its flag is highlighted), top right moves it by the Grid, bottom right swaps it.
28. **Loop popup.** Top knobs move Loop Start / End by the Grid; bottom left halves / doubles the loop; bottom right moves the whole loop by the Grid; **Nudge** moves it one Grid step; **Instant** puts it at the white marker. Loop 4 bars, wait until the playhead is in bar 3, halve: the loop becomes bars 3–4 and keeps playing (it does not jump out). Turn the Grid knob: it steps 1/4 … 16 bars (not below the clip's Sync) and its title follows at once. **Escape:** loop on, playhead in the loop, turn Loop End to the left past the playhead, then move the loop (bottom right) a bar later and a bar earlier, and tap Nudge: playback stays inside the loop every time and moves with it, in time. Tap a cue past the loop: it plays on from there to End, then loops. **Instant** with the marker just after a bar line, then just before the next: the loop starts on the nearer bar.
29. **Grid popup.** Grid lines show as soon as the waveform opens, without touching a knob: a line per beat (per Grid step at 1/8, 1/16), thinner when zoomed out, and they stay all along the file while you scroll, zoom and play. **Lines** turns them off and on. Bottom right zooms (main layer and Grid popup); the title shows the screen's width. On a file that does not start on a downbeat, turn top left (`Nudge +…ms`) until the lines sit on the kicks: Start, the loop and the cues move with it, and the music plays in time from the new grid. `< 1` / `1 >` shift the grid by one Grid step. **Raw** (lit): knobs move 8× faster.
30. **Play from the cursor.** Open `...`, scroll the white marker into bar 9, press the pad (or Play with a pattern that triggers it): it starts at bar 9.1. Close `...`: it starts at Start again.
31. **Keys.** A Clip with any cue set (Cue A is set by default) opens the Keys screen on 8 keys A–H and CUES, set cues lit like an "on" toggle. They play the cues. With Cue Note 60, they still play the cues. STOP does nothing on this pad. Another pad (Slicer) shows its slices as stock.
32. **BPM name.** Copy a 5-minute stem named `… 128bpm.wav`, Beat Count Auto: it plays at the right speed, in time with the transport (stock would take 512 beats). Rename to remove the BPM: back to stock Auto.
33. **Beat Count 1024.** In the Clip Pos list, Beat Count reads `Auto` at the left end and goes past 512 up to 1024. Load a 7-minute stem at 128 BPM (about 900 beats), set Beat Count to its real length: it plays in time with the transport. Save, reload: the value stays.

If the device disagrees with the map, add a bench case or a named fact first, then change the cave.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-clip-deck/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-clip-deck/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The bench runs the folder, Warp and slice cases, plus the Clip region cases:
- the Pos ids;
- registry names;
- bag defaults;
- the region math for Sync 1/4, 1 bar, None and Slice, loop on/off and Trigger;
- the engine `0x39` message;
- the two grain stubs, which must keep `r0`–`r2`, the three stack arguments, `lr` and `sp`;
- the limit stub's registers;
- the cue ids in Pos, their names and list labels (`-`, beats; Cue Note uses Pad Note names), bag defaults and set-insert;
- cue values from the UI post and engine msg `0x39` into the SRAM4 table, and the re-init from the P1 magic;
- the jump engine: launch on a stopped clip, pending jump on a playing one, Quant Size 1 bar / None, negative ticks, clamping, Loop End vs. a pending jump, Cue Mode off, Sync None, Slicer, other pads;
- `hook_voice`, which must keep `r0`–`r2`, both stack arguments, `lr` and `sp`, and skip the call for a jump;
- stock facts of the MIDI note path (on every image);
- End / Loop End 512 on a 1000-beat file and on a Sync 1/8 file ending in half a beat, and the 9-character bar name;
- the editor on a real stock waveform view (constructed by `FUN_080A5B90` on the bench, with stock's whole update and the real event handler): `...` visible, titles as shown on screen (`knob+0xF4`) after `...` and after each tab, tabs, 12 buttons built once, Slicer restored, popups at `{20, 50, 280, 40}` and a touch in the box reaching the button (or the box), Cue letters (store at the white marker, play, hold Cue + tap to clear / set, knob clear), cue flags and the highlighted one, main / Loop / Grid knob functions by the Grid, Grid clamped to Sync, Raw ×8, grid lines (on by default, per beat or finer Grid step) and offset; the Clip param template keeping all deck ids (list capacity 96); the loop never escaping when its markers move (the playhead moves with it); grid lines and flags rebuilt on every waveform draw; zoom; play from the white marker's bar while editing;
- the Pos-list text for cues and positions (`hook_list_value` at `0x080A9218`) and its stub;
- the grid offset in `deck_grain_start` and the `hook_grain_ofs` stub (`fp`, `lr = 1`, registers, `s0`–`s14`), and in the cue prefetch;
- the Keys hooks (letters, count, refresh, note map, STOP) and `hook_keys_on`'s registers and stack argument;
- the BPM parser on 13 names, 640 beats for a 5-minute 128 BPM file, and `hook_beats`'s registers including `s0`–`s15`;
- no SRAM4 writes outside the deck block, and no use of that block in the Bungee worktree's sources.
- the cue prefetch: the seek on the note and on every grain until the jump, the busy-ring skip, an untouched clip, no card reads, and stock's seek queuing type-4 block requests and waking pcmStreamer on a fake manager.
