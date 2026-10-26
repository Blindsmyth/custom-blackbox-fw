# Song DJ row: Beatjump and Autoloop on the Song screen (3.1.T)

Status: built on `feature/song-dj` (from `main` at 4550644). First tried on the unit 2026-10-25: the row, beatjump and loop work. Fixed after that test: notes missed in small sequence loops, and a scene change when the loop was resized at the scene's end. The menu reads `3.1.T DJ`. Code: `firmware/patches/3.1.9-song-dj` (`dj.c`, `dj.S`, `sites.py`), plus `dj_then_loop` in `firmware/patches/3.1.9-clip-deck/deck.c`.

Inspired by Mixxx and rekordbox: beatjump and autoloop for the whole set at once, with every playing Clip pad, pad sequence and the song moving together.

## How to use it

- **Open the row:** on the Song screen, press **SONG** again. A row of three buttons appears at the bottom: **<< 16 | 16 >> | Loop 1**. SONG again hides it, and leaving the screen hides it too. SONG from any other screen opens Song as stock does.
  - The row is shown in the scene list and with the Edit popup open. It is hidden while a scene's sequence or pad grid is open.
  - The row covers the lowest ~38 px of the scene list. Scroll to reach a scene under it.
- **Beatjump:** **<<** / **>>** move everything back or forward by the jump size. The move happens on the next beat and keeps the phase.
  - **Bottom-left knob:** halves or doubles the jump size, from 1/4 bar (one beat) to 64 bars. The default is 16. One step is one stock list step of the knob (400 counts).
- **Autoloop:** **Loop** loops everything at the loop size. The button is lit while the loop is on. Press it again and everything plays on **from inside the loop**: the classic DJ loop, not rekordbox's slip mode.
  - **Bottom-right knob:** halves or doubles the loop size, from 1/16 to 16 bars. The default is 1 bar. With the loop on, a new size applies at once.
    - **As Mixxx and rekordbox:** a new size keeps the loop's start, and only the end moves.
      - **Halving** with the playhead past the new end jumps back in whole loop sizes, keeping the phase. For example, a 16-bar loop halved in bar 12 jumps to bar 4.
      - **Doubling** plays on, and the end moves out.
    - **With the song on, the window stays in the scene.** A loop that would end past the scene's end is moved back to end on it, so it grows before the playhead and never runs into the next scene.
  - **Where a loop starts:** on the **beat closest** to the press, as Mixxx's `findQuantizedBeatloopStart`. A loop under one beat starts on the closest fraction of the beat. If the closest beat is just ahead, the loop is a catching one: the playhead runs into it.
  - A **beatjump while looping moves the loop**, as Mixxx does.
- **Sizes are in bars** on the buttons: `1/4` is a beat and `1/16` a sixteenth.
- **BPM and Swing** stay on the top knobs.
- **The SONG LED blinks** while the row is open.

**What moves:**
- every playing **Clip** pad on a note-value Sync grid, the same Clips that Clip Deck works on, in every Warp mode;
- every playing **pad sequence**;
- the **song** itself, with Song playback on.

**What doesn't:** Slicer pads, and Clips with Sync None or Sync Slice. The transport and its bar counter never move.

**A Clip started after a press** does not take that jump, and does not join a loop that was already on. A sequence that starts while the loop is on does join it.

### Scene walls (Song playback on)

The song moves with the jump inside its scene, so the scene ends correspondingly earlier or later.
- **A jump that would leave the scene is not taken.** That means past the scene's end, or past its start going back. With the loop on, the whole loop window has to stay inside the scene.
  - Instead, the scene the jump would have reached **blinks three times** in the list. That is the next or previous scene, as the song moves on: with Loop it wraps, with Loop1 it is the same scene, and at the song's end it is the current one.
- **The loop is cut on the scene's grid** and never crosses the scene's end. For example, on beat 15 of a 16-beat scene a 1-bar loop plays beats 13–16. With a scene length that is not a whole number of loops, the last loop is moved back to end on the scene's end: on beat 18 of an 18-beat scene, 1 bar plays beats 15–18.
- **While the loop is on, the song stays in its scene.** The loop holds the song as well. A wrap back over the scene's first beat does not count as a scene start, so the scene's pad and sequence starts do not fire again.
- **Without Song playback**, there are no walls and the loop sits on the global bar grid.

## How it works

### One timeline (audio task, `dj.c`)

- **The UI posts requests.** Each press bumps a sequence number together with a size in ticks: `jump_seq` / `jump_ticks` and `loop_seq` / `loop_ticks` in `DjState`, SRAM4 `0x38007400`.
- **The first user in the audio task arms a request.** A user is a Clip grain, a sequence step or a song step.
  - **Jump:** taken on the first beat at least 240 ticks (1/16) after that user's tick (`armed_b`). With Song playback on, it is checked against the scene first. A jump nobody plays within 300 ms of the press is dropped, so starting something later does not jump it.
  - **Loop:** a window `[loop_a, loop_a + loop_len)` in transport ticks, following Mixxx's `LoopingControl` (`slotBeatLoop`, `findQuantizedBeatloopStart`, `adjustedPositionInsideAdjustedLoop`).
    - **A new loop** starts on the closest beat, or on the closest multiple of the size under one beat (`round_to`). With the song on, that is measured on the song's position `p` in its scene, after its own loop has taken it back. `loop_a = t − (p − start)`.
    - **A new size** keeps the start. Without the song, `loop_a` moves to the start of the pass the loop is in. With the song, `loop_s` stays, and `loop_delta` records how far a cut on the scene's end moved it back.
    - **Scene end:** a window ending past the scene's end is moved back to end on it.
  - **Switching** (`dj_shift`): each user switches at its first unit or step at or after the arming tick (`loop_from`). On that step it skips the old loop's wrap, because the switch covers it.
    - A user already looping takes its place in its old loop, after a wrap that is due, less `loop_delta`. Any other user takes `t − loop_a`.
    - Past the new end, it goes back in whole sizes (a halved loop jumps into its first half). Before the start, it is a catching loop.
    - Its next wrap is one size after the loop's start.
- **Each user turns that into its own units or steps** (`dj_shift`):
  - **jump:** at its first unit or step starting at or after `armed_b`, it moves by the jump in whole units or steps, rounded toward zero. A Clip with 1-bar units therefore ignores a 1-beat jump.
  - **loop:** every `lp` ticks from `loop_a`, it moves back by `lp`. `lp` is the loop rounded up to whole units or steps.
  - Sequences go round their step count, and Clips go round their Clip Deck region (or the whole file). A 16-bar jump on a 4-bar sequence therefore lands on the same step.

### Clips (`deck.c`)

- **`deck_wrap_unit`** (the next grain, at `0x08066CF2`) calls `dj_then_loop`. That function applies `dj_clip_unit` after Clip Deck's own cue jump and before its loop (`in_loop`). A move counts as a jump for the Clip's own loop.
- Fill, Chop, Grain and Bungee go through the same code: bank 2's `DeckSeq` calls `deck_wrap_unit`. Bungee re-launches at a jump or a loop wrap, as it does for cues (raw Clip first, Bungee ~43 ms later).
- **A coming jump is prefetched** like a cue (`dj_clip_ahead` → `cue_prefetch`).
- **`deck_note_unit`** calls `dj_clip_start`, so a Clip started now does not take a request made before.

### Sequences and the song (`dj.S`, two Player_Tick sites)

- **`0x0805DD34` (`hook_dj_step`)** replaces `ldr.w r4, [fp, #0x20]`, the current step. Stock adds 1 to it and compares the result with the step count in r7.
  - `dj_seq_step` reads this step's tick (`[sp+0x20]` / `[sp+0x14]`) and length (`[sp+0x50]`) from Player_Tick's frame.
  - It returns the step stock adds 1 to. The next-step time is computed from the transport, so only the index changes.
  - **A pad sequence's notes are moved too** (`seq_seek`). Stock fires a layer's events by position from the pass start (`layer+0x38`, ticks), with a cursor (`layer+0x14`) into its sorted event list (`layer+0xC`). Only step 0 resets the base and the cursor. So for a move to step k, the pass start goes to this tick − k steps, and the cursor to the first event at or after step k.
    - In step mode (`+0x5E`), event positions count steps of 960 at the current step length.
    - Before this fix, only the step index moved, so a loop that didn't start on the sequence's first step missed or misplaced notes.
  - **The song's position is recorded after its move is decided.** A loop armed on the song's own step therefore still knows the scene. The song player is also recognised while its flag is off.
- **The song is the sequence player with `+0x60` set.** Its steps are one beat each (step length index 6), and its step count is the scene's beats × Play Count (`FUN_0809C2C8`). Its position is therefore the step index.
  - **When the song is moved to a step,** its song flag is cleared for that step. `FUN_0805D7E0` then does not switch scenes on a step 0, and `FUN_0805CDA0` does not fire the scene's items.
  - **`0x0805DDDC` (`hook_dj_step0`)** wraps the `bl FUN_0805CDA0` (the step's events). It passes the stack argument through untouched, then calls `dj_seq_done`, which puts the flag back. It also reads a new scene's length once `FUN_0805D7E0` has switched the layer.
- **Walls** use the song's last step and its tick, so the scene start in ticks is `song_tick − song_step × 960`.

### The Song screen (UI task)

- **SONG button.** `0x080A329E` (`bl App_SetScreen(…, 0x2D)` in `App_HandleInput`) goes to `dj_song_key`. With the Song screen up (`App+0x8CA4 == 0x2D`), that function toggles the row instead of switching screens.
- **Song vtable:**
  - **+0x00 tick** (`dj_song_tick`) builds the row once, then shows or hides it, labels it, lights Loop and runs the blink.
  - **+0x30 onChildEvent** (`dj_song_child`) acts on the press of our buttons, ids `0x1D0`–`0x1D2`.
  - **+0x34 onEvent** (`dj_song_event`): with the row shown, encoders 1 and 3 set the sizes. Event 0xD3 (leaving the screen) hides the row.
- **The buttons** are three stock frame buttons in SRAM4 (`DjState.btn`), each {x, 0, w, 38}: 0/99, 100/99, 200/120. They are latched like the scene rows, so they draw in the partial pass.
  - They are linked in as the Song widget's **oldest** children: the draw walks newest first, and the hit-test walks oldest first. They are therefore drawn over the list and hit before it.
  - Hiding them marks the scene rows dirty, so the list paints again.
- **The blink** toggles the target scene row's selected look (`FUN_080C2318`) every 100 ms for 600 ms, then restores it.

## Memory

`DjState` sits at SRAM4 `0x38007400`–`0x38007A58`. That is free on 3.1.T: it is past RbVoice and diag (`0x38007000`–`0x38007148`) and below Bungee's grain input (`0x38008000`). It also stays clear of Song cues' `SgState` (`0x38006400`–`0x38006BDC`, on `claude/eloquent-lichterman-6eca6a`). `dj_init`, called from `deck_init` at boot, clears it. Nothing is saved in presets, and the pads' own Clip Deck loop settings are never touched.

## Bench

```bash
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-mix-bungee/lineup/BLACKBOX.BIN --only "Song DJ"
```

These cases call the C directly with fake players and a fake Song widget:
- **Clip:** jump on the beat; round the file; a stale jump is dropped; the loop on its grid; a jump while looping; loop off plays on; a Clip started later stays out; coarse units.
- **Sequence:** jump; round the steps; a starting sequence is left alone; the loop wraps; steps longer than the jump stay put.
- **Song:** moves inside the scene; refused jumps and the scene to blink; the song flag around a moved step; the loop cut on the scene's end and holding the scene.
- **UI:** SONG toggles the row; buttons; knobs; labels; the row is hidden in the grids; the blink; leaving the screen.

**Not covered:** the two Player_Tick stubs inside a real Player_Tick, and drawing on the panel.

## To check on the unit

- The row draws over the scene list, and the list paints cleanly again when the row is hidden.
- Font width: "Loop 1/16" is 9 characters (108 px) in a 120 px button.
- A jump and a loop with Fill, Bungee and a pad sequence together stay on the clock (Bungee's Warp label shows re-launches and lock error).
- With Song playback on:
  - the walls and the blink;
  - the scene ends one bar early after a 1-bar jump;
  - the loop holds the scene;
  - no pad restarts when the loop wraps over a scene start.
