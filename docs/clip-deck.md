# Clip overhaul: regions, cues, beat jump, loop roll

Goal: DJ/Ableton-style control of long samples and stems on Clip pads, **without a new pad mode**. A new cellmode would touch every per-mode table (Info pages, waveform screen, XML, engine switch), and stock could not load such presets. Clips that use none of the additions behave exactly like stock.

| Phase | What | Status |
| --- | --- | --- |
| P1 | Play Start / End, Loop Start / End, Loop button on the waveform page | Built as [3.1.E](../firmware/patches/3.1.9-clip-deck/README.md), bench only |
| P2 | 8 MIDI hot cues per Clip (built); transient marker list (roadmap) | Hot cues built in [3.1.E](../firmware/patches/3.1.9-clip-deck/README.md#hot-cues-phase-2), bench only |
| P3 | Beat jump and loop controls on MIDI notes | Roadmap |
| P3a | Clip editor: `...` row with Cue / Loop / Grid popups at Scan's place, Grid size on the main layer, positions in 1/16 notes, grid offset in the engine, A–H Keys layout, Beat Count from a BPM in the file name | Built in [3.1.E](../firmware/patches/3.1.9-clip-deck/README.md#clip-editor-phase-3), bench only |
| P4 | Master loop roll ("loop the last bar of everything") from the stock Repeater | Roadmap |

## Units

The Clip engine plays one grain per **Sync unit** (`clip+0x1FC` samples, `clip+0x208` ticks; 960 ticks = 1 beat). Every position is stored in beats and snapped to that grid.
- With Sync None the file is one grain, and with Sync Slice the units are slices. In both cases P1–P3 do nothing on that clip.
- A beat-based value has one exact step per beat at any length; the stock type-`0xD` knobs only resolve 1/1000 of the sample.
- Loops shorter than a unit need shorter grains (P4).

## Transients vs. slices

Keep them separate. The slice table is what stock Slicer, Sync-to-Slice and the 3.1.Y `ext` extension read, so it stays.

Transients become a per-pad **marker list**:
- **Filling it:** on demand, from `detect_run` (`tools/stretch/capi.c`, already on device inside Capi). `beats_detect` (`tools/stretch/beats.c`) is the cheap fallback. The stock Scan detector (UI messages `0x47`/`0x48` from `FUN_080A66DC`) may already do the job, so locate it first.
- **Using it:** cue and loop edits snap to the nearest marker when Snap is on.
- **Storing it:** an `ext`-style attribute on the cell (`sl_write_ext` / `sl_parse_ext` in `slice.S`).
- **Watchdog:** the detector runs in chunks on the pcmStreamer side, and those chunks stamp the watchdog (see [re-notes.md](re-notes.md#tasks-card-access-watchdog)).

That avoids thousands of slices on a 5-minute stem, while slices stay available for chopping.

## P2 — hot cues

Built in 3.1.E (bench only). What the trace found and what changed from the plan:
- Only the pad's own **MIDI In** channel (`midimode`) delivers the real note to the voice start. Global In / Pad Note triggers pass note 0, so cues listen on the pad's MIDI In channel. Full path: [map-319.md](map-319.md#midi-note-path-pad-trigger).
- Gran (Info page 3) was not used: no tab-visibility code was found, so the params are at the end of Pos.
- Cue params are list params (`-`, 0–511): the stored value is the list index, 0 = unset. The SRAM4 table keeps beats with `0xFFFF` for unset.
- The jump is taken in `deck_wrap_unit` (every next grain), keyed on the grain's start tick being a multiple of Quant Size, not in `deck_limit`. A cue on a stopped clip launches it at the cue.

Original plan:


- **Params:**
  - `cue1..cue8`: beats, or `-` for unset.
  - `cuenote`: base note, default 36.
  - `cuemode`: Off/On.

  Clip Info page 3 (Gran) is empty on Clip. Check on the bench whether filling it brings the tab back; otherwise append the params to Pos.
- **Trigger:** hook the three `bl FUN_08065908` sites in `FUN_08067FA4` (`0x080683AE`, `0x08068656`, `0x080686CC`). blackbox-mod's `duck_voice_start_b` wraps the same sites.
  - If cuemode is on and the note is in `[cuenote, cuenote+8)` on a Clip that is already playing, set a pending jump unit instead of retriggering.
  - P1's `deck_limit` / `deck_wrap_unit` path takes the pending jump at the next Quant Size boundary (`clip+0x218`). It works like a wrap with a different target.
- **Trace first:** the path from the MIDI parser (`0x08044708`) to engine message `0x42`, which is where channel and `midimode`/`padnote` filtering happen.

## P3 — beat jump and loop controls

- `jumpsize`: 1, 2, 4, 8, 16 or 32 beats.
- Notes `cuenote+8` / `+9` jump back / forward, using the same pending-jump path as the cues.
- Notes `+10` toggle the loop, `+11` / `+12` halve / double the loop length, and `+13` moves the loop by its own length.
- Everything is reachable from a MIDI controller. Hold FX + pad is a free gesture if one is wanted later.

## P4 — master loop roll

The stock Repeater is a master-bus FX (`SE+0x28D90` slot 0, process `FUN_0806d858`, param handler `FUN_0806d654`, trigger `FUN_0806d588`).
- **What it has:** an **8 s stereo history ring** in SDRAM (2 × 0x177000 bytes, write index `+0x34`), and it captures retroactively on the grid.
- **Its limits:** the length table (`0x080ECBC8`, at most 1 beat) and the 2 s loop buffer.
- **Plan:**
  - Extend `repeatlen` with 1/2 bar, 1 bar and 2 bars.
  - While held, freeze the ring's write index and loop straight out of the ring, starting at `gridTick − len`. No new SDRAM, so sample memory is untouched.
  - Clamp the length to what fits in 8 s: 2 bars at 60 bpm, 4 at 120.
- **Limitation:** the Repeater sits on bus 0 before the compressor. Pads routed to Out 2/3 mix in later (`FUN_080617c0`), so they are not captured.
