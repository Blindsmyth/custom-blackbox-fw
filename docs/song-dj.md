# Song DJ row: Beatjump and Autoloop on the Song screen (3.1.T)

Status: merged into `main` (built on `feature/song-dj` from `4550644`). First tried on the unit 2026-10-25: the row, beatjump and loop work. Fixed after that test: notes missed in small sequence loops, and a scene change when the loop was resized at the scene's end. The menu reads `3.1.T`. Code: `firmware/patches/3.1.9-song-dj` (`dj.c`, `dj.S`, `sites.py`), plus `dj_then_loop` in `firmware/patches/3.1.9-clip-deck/deck.c`.

Inspired by Mixxx and rekordbox: beatjump and autoloop for the whole set at once, with every playing Clip pad, pad sequence and the song moving together.

## How to use it

- **Open the deck:** on the Song screen, press **SONG** again (the deck view below). SONG again closes it. Song remembers which one it was in until power off.
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

## Mixer: audio loop, crossfader, DJ filters, global BPM

Status: built and bench-tested (2026-10-27), **not yet tried on the unit**. Code: `firmware/patches/3.1.9-song-dj/mixer.c` (audio and BPM) and the Song-screen parts of `dj.c`.

For going from one project into the next in a live set: capture a loop of the master, load the next project under it, and crossfade into it.

### How to use it

- **Tools → Clock → BPM: Project / Global.** With Global, loading a preset keeps the tempo that is playing. The preset's BPM is overwritten in the session, so the Song screen and the next save show the kept one. While an audio loop is held, the tempo is kept whichever setting is chosen. Swing still comes from the preset.
- **Capture:** open the DJ row (SONG on the Song screen), turn **Loop** on, then press **BACK**.
  - The DJ loop is switched off at once, so the project plays the current pass to its end and carries on past it.
  - At that end, the **audio loop** starts. It is the pass that just played, so the switch is seamless, and the crossfader goes **fully left** (loop only).
  - The audio loop appears in the waveform's top lane.
- **While the audio loop plays:**
  - The **top-right knob** sizes the audio loop, and the project's loop with it: there is one loop size. A new size keeps the start, as Mixxx does.
    - Bigger sizes play what the project really played after the loop. It was recorded while the loop played: up to 16 bars past the loop's start, until a preset load starts.
    - A size that hasn't been recorded yet waits until it has.
  - **Load the next project.** The transport keeps running and the loop plays through the load. Then slide the crossfader right to bring the new project in.
  - **Loop** loops the new project as usual (shown in the bottom lane), while the audio loop keeps playing.
  - **BACK** erases the audio loop. The crossfader goes back fully right and the ring records again. To grab a loop of the new project, press BACK again once the crossfader reads `BACK: capture loop`.
- **DJ mode is remembered** until power off: leaving the Song screen with the deck open and coming back (or SONG from another screen) opens the deck again. SONG on the Song screen still switches between normal and DJ mode.
- **The deck view** (SONG pressed again on the Song screen) replaces the scene list and the top bar while it is open, and gives them back when it closes:
  ```
  182-220  [ << ][ 16 ][ >> ][ ▲ ][ ▼ ][  Loop 1  ]   (44 | 40 | 44 | 36 | 36 | 120 px, 1 px lines between)
  164-180  3 Drop  > 4 Break           (scene playing, then queued / next)
   78-162  audio loop lane (cyan) over project lane (pink)
   46- 76  [---O---][Loop] | [Proj][---O---]   (88 + 72 px each side: the toggles meet in the middle)
    0- 44  L [====[#]====] P           (crossfader)
  ```
  - **Top bar:** beatjump `<<` / `>>` with the jump size between them, scene **▲** / **▼**, and **Loop** at the far right. Each button is 1 px narrower than its slot, and the panel draws a short line in each gap.
    - The arrows do what a scene row press does (event `0x107`): with the song playing, that scene starts on the next bar. They count from the scene last pressed or queued and stop at the first and last.
  - **Scene line:** the scene playing and the one queued (or the next).
  - **Waveform:**
    - **Audio loop lane (top):** only once BACK has grabbed a loop: the audio loop, with a tick on every bar and a white playhead. Empty otherwise.
    - **Project lane (bottom):** with the Loop button's loop on, one whole pass of it (bar ticks, playhead; ahead of the playhead the pass before). Otherwise the project's level over the last ~1.7 s, newest on the right.
  - **Filters:** two horizontal sliders, loop on the left and project on the right, as on the crossfader.
    - Bipolar like the pad Filter: left of centre is low-pass, right is high-pass; dragging near the centre snaps Off.
    - **Loop / Proj** (the captions) take their filter out and back in. The slider keeps its place, drawn grey while out. They are lit while the filter is in.
    - **A capture resets both filters**: centred and in.
  - **Crossfader** (bottom, touch): a track that is dark at both ends, where one side is silent; a white knob; the loop's (cyan) and the project's (pink) level as bars at the edges.
    - Each side stays at full level up to the middle and only then fades out (a cosine quarter), so both are full in the middle. The last 5% of travel at each end has the other side fully down.
    - Before a capture it reads `Loop on, then BACK`, then `Recording the loop...` until the pass playing now is fully recorded, then `BACK: capture loop`.
  - **Knobs follow the screen:**

    | Knob | Controls |
    | --- | --- |
    | Top-left | jump size |
    | Top-right | loop size (the project's loop and the audio loop together) |
    | Bottom-left | loop filter |
    | Bottom-right | project filter |

### How it works

- **Tap:** `mx_comp_tap` replaces the compressor's gain-ramp `bl FUN_0804BE40` at `0x0804EE9A`, inside `FUN_0804EDDC`. That is after the compressor and before the limiter.
  - With the compressor off, `mx_bus_tap` does the same where both render paths meet. It replaces `bl FUN_0804D9C0` at `0x0805354A`, which gets bus 0's L/R before the output volumes.
  - A per-block flag (`done`) keeps it to one run per block.
- **Ring:** 16384 blocks × 128 frames of int16 stereo (43.7 s, 8 MB) plus one anchor per block: the block's start in 1/65536 ticks.
  - **Borrowed from stock's sample cache, only while needed.** SDRAM has about 1 MB free after boot (measured on the unit), so the ring can't be a reservation of its own: an 8 MB grab at boot hung the unit. Instead, while a DJ loop is on or an audio loop is held, 131 of the cache's 615 entries are borrowed. Each entry is 8192 stereo frames as two 32 KB float buffers (`+0x04` L, `+0x08` R), 64 KB in all.
    - 128 entries hold the audio, 2 hold the anchors, and 1 is scratch for a block's loop audio.
  - **The cache:** the sample manager is the object at `0x2400A9C0` (pcmStreamer's loop argument), with entries every `0x1C` bytes. Each entry's fields run `+0x04`..`+0x1F` (they overlap the next entry's first word): L, R, block `+0x0C`, LRU stamp `+0x10`, frames `+0x14`, wave slot `+0x18` (0xFFFF none), next pair `+0x1C`, pair index `+0x1E`, state `+0x1F` (0 free, 1 loaded, 2 recorded, 3 reserved).
  - **Borrowing** (`mx_stream_poll`, from `b2load_thunk` in pcmStreamer's loop, which owns the cache): stock's picker `FUN_08071D64` hands over an entry the way it does for a block load. It takes a free entry, otherwise the least recent; it unmaps that from its wave and frees chained pairs. We then mark it state 3 with no wave: eviction skips state 2/3, and every lookup matches on the wave slot, so stock never touches it. If the picker falls back on one of ours (nothing evictable), the borrow stops and retries on the next poll.
  - **Giving back:** once nothing wants the ring, the audio task must first stop using it (two blocks), then each entry is reset as stock frees one (`FUN_08072678`).
  - **Cost:** while you loop, the cache is 484 entries instead of 615 (about 83 s instead of 105 s). Samples that were in the borrowed entries stream from the card again when needed. Stock pad recording needs 128 contiguous free entries, so recording to a pad while a loop is on may fail.
  - **Ticks:** the engine's are `SE+0x30` (u64); the tempo is `SE+0x18` (float BPM); playing is `SE+0x38`. SE = `*(0x2400A9C0+0x9A74)`. Our own fractional tick runs at the tempo and is pulled back to the engine's when they differ by more than 4 ticks.
- **Capture** (`mx_capture`, from BACK in `dj_song_event`):
  1. The UI hands over the DJ loop (`loop_a`, `loop_len`) and the `loop_seq` that switches it off.
  2. The audio task sets W = the first `loop_a + k·len` (k ≥ 1) at or after the off's `loop_from` (or now, if no user has armed it yet), and A = W − len.
  3. At W it finds A's block through the anchors and plays from there.
- **Playback:** the loop reads tick `A + ((t − B) mod len)` through the anchors. Mapping by tick is what keeps it on the bar grid and makes it repitch with the BPM.
  - A new size moves the phase origin B so that the place in the loop is kept; a smaller size goes back in whole sizes.
  - At the seam, the window's end runs on into what was recorded after it, faded over 96 samples.
- **Load** (`mx_load_begin`, at `bl` msg 0x36 `0x0809E258` in `SessionMgr_LoadBank`): it stops the held loop's recording and reads BPM Global.
- **BPM** (`mx_bpm_send`, at `bl FUN_0804C59C` `0x0809CE82` in `SessionMgr_FinalizeBankLoading`): id 0x84 isn't sent to the tempo node; `Param_Set(App+0x3038, 0x84, playing BPM)` is done instead.
- **Filters:** RBJ biquads with fixed Q 0.8. The cutoff comes from the pads' knob curve (`FUN_08060640`): LP from `1 + v`, HP from `v`. The knob is smoothed per block, and the coefficients are recomputed only when it moves.
- **Crossfader:** with x the travel between the two 5% dead zones (0..1): left of the middle gL = 1 and gR = sin(x·π); right of it gL = cos((x − ½)·π) and gR = 1. Ramped over each block. Without a held loop, gR = 1.
- **Deck view widgets:** stock frame buttons (`BTN_CTOR`, 0xA0 bytes) on the Song widget, each added as its oldest child, so it is drawn after stock's and hit before it.
  - **Own vtables:** the panel, waveform, sliders, crossfader and arrows use copies of the button vtable `0x080EFE48` with their own draw (vt+0x04); the panel, sliders and crossfader also have their own touches.
  - **Drawing:** stock's `FILL_RECT` `0x0808EA23` / `OUTLINE` `0x0808E995`, with palette 2 background, 17 track, 25 dead ends, 16 ticks, 3 knobs, 5 loop, 4 project. The arrows are rows of `FILL_RECT` over the stock button draw, with no font glyph. Widgets are marked dirty (+0x31/+0x32) when their value moves; the waveform repaints at most every 50 ms.
  - **Widths:** a stock button label shows the tail of a text that is too wide for it (`Loop` in 48 px read `op`), so the captions are 72 px and Loop is 120 px, enough for `Loop 1/16`. A label also keeps its scroll offset while hidden, so every label is set again when the deck opens (`relabel`).
  - **Repaints are asked for by our tick.** The GUI only runs a repaint pass when a screen tick raises its byte (the tick's flags: `[0]` full, `[1]` partial), and stock's Song tick knows nothing of our widgets. So `dj_song_tick` raises the partial pass when one of ours is dirty, and both passes when a label changes or the deck opens or closes (as the Mix screen does). Without it, our changes waited for an unrelated stock repaint: labels came out half-drawn, and closing the deck sometimes left the screen black.
  - **Labels are set only when their text changes** (`keep_text` compares the button's text at `+0x65`: the label is embedded at `+0x34`, its text at label `+0x31`). Setting the same text every tick also re-lays the label out and repaints it, and that jammed the labels.
  - **Closing the deck** marks the whole Song widget tree dirty. `HIDE` alone only marks the container, so its rows and buttons stayed black until something else repainted them. The walk follows the parent links (+0x2C) without a stack: a 12-deep stack had dropped most of the tree (1140 of 1184 widgets on the bench), which is what still left normal Song mode black at times. Labels (vtable `0x080F17A4`) are passed by: their text starts at +0x31, where other widgets keep the repaint bytes, so marking them wrote over their first two letters ("Edit" read "it", "Loop" "op"). They paint with their parent anyway.
  - **Leaving the screen** (event 0xD3) closes the deck at once and keeps DJ mode asked for, so the first tick back opens it again.
  - **Stock hidden:** while the deck is shown, the scene list container (`Song+0x1067C`) and the top bar (`Song+0xFFE4`) are hidden. They are hidden again if a refresh shows them, and restored to their own state when the deck closes.
  - **Memory:** the deck's 10 own widgets, the project peaks with their ticks and the lane buffers (`DjDeck`, ~23 KB) come from the bottom SDRAM allocator once at boot (`dj_init`); the crossfader and the deck state stay in SRAM4 (`DjXf`).
- **Waveform data:**
  - **Audio loop lane:** `ring_put` stores one peak byte per ring block in the scratch entry, after lp/rp. `mx_wave_loop` maps columns to blocks through the anchors (only while an audio loop is armed or held), and sets `ui_busy` so the give-back waits until the drawing is done (`mx_wave_done`).
  - **Project lane:** the audio task keeps one peak per 4 parts (~10.7 ms) of the project, before our mix, with its transport tick, in `DjDeck.proj` / `proj_t` (4096 each, ~43.7 s). `mx_wave_proj` reads the newest; `mx_wave_pass` maps a loop pass's columns to peaks by tick (binary search from `proj_v`, the first peak since the transport last went back). It needs no ring, so the project's loop shows while the ring holds the audio loop.
- **Scene buttons:** `dj_song_child` posts `0x107` with the scene (`ui_post_event_up` `0x080AED14`). The app's `FUN_0809B7B0` queues it (`0xB2` to key `0x400`) when the song plays.
- **Scene line:** the song player's playing layer (`+0xC20`, index `+0x40`) and queued one (`+0xC24`), or `App+0x20` with the song off. Names are the str at `App+0x29C0 + i·0x30 + 0x2C` (`str_cstr` `0x080460BA`).
- **BACK:** hardware BACK reaches the app as `App_HandleInput` type 7; `hook_type7` (preset folders) now asks `dj_back_key` first.
- **Setting:** param `0x1D8` (`bpmglobal`, Project/Global). It is registered from `deck_register` and added to the global bag from `seqrec_global_tpl`, and the Tools → Clock list slot `0x080EEDF8` = `0x1D8`.

### Not done yet

- **Next bar on load.** The new project's sequences and song are not yet cued to the next bar after a load. The transport keeps running, and stock's Quant Size still decides when a launched sequence starts.
- **On the unit:** the borrowed ring hasn't been tried yet. Check that loops with many long Clips still stream cleanly while a loop is on.
- **Pads routed to Out 2/3** aren't on bus 0, so they aren't captured or crossfaded.
- **DJ FX routing (planned).**
  - **Layout:** on the DJ FX screen (0x37, constructor `FUN_080B20F0`), Hold A moves to y 176 and Hold B to y 56, by changing the `movs` immediates at `0x080B231E` / `0x080B2352`. Under each Hold button go two toggles, **Proj** and **Loop**, which pick what goes into that FX slot.
  - **Knobs:** bottom-left is the loop filter and bottom-right the project filter. Holding INFO gives the knobs back to stock, to swap FX.
  - **Audio:** the mixer stage moves to just after the master EQ (`blx` `0x08053526`), before the FX container `SE+0x28D90`. Each slot then processes the sum of what is routed to it, by swapping bus 0's L/R pointers around the slot.
  - **Open question:** should the capture point move to before the FX, so loops are captured dry?

### Bench

```bash
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-mix-bungee/lineup/BLACKBOX.BIN --only "mixer"
```

The cases call `mixer.c` with a fake engine clock and a saw on L:
- capture at the DJ loop's end, played back by tick;
- 2 bars waits for the recording and then plays the bar that followed;
- a preset load stops the recording;
- 140 BPM repitches and stays on the grid;
- half a bar applies at once;
- the crossfader centre is constant power;
- BACK erases the audio loop;
- LP / HP / centre;
- BPM Global vs Project;
- BACK, the knobs, the shared loop size and Loop on the Song screen;
- the project's loop pass in the waveform.

**Not covered:** the taps inside a real render, and drawing.

## Memory

The mixer's `MxState` follows it at `0x38007A60`–`0x38007C64` (fits below `0x38008000`). The crossfader and the deck's state (`DjXf`) are at `0x38006F00`–`0x38006FE0`, and the deck's own widgets (`DjDeck`, ~23 KB) come from the bottom SDRAM allocator at boot, between Song cues' `SgState` and RbVoice. The ring and its anchors live in 131 borrowed sample-cache entries, only while a loop is on (the entry indices are in `MxState.ent`).

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
