# 3.1.T: Warp lineup + Clip Deck

One image with everything on this line:
- **The base:** 3.1.Q: folders, the Mix screen, Seq Rec, the looper, Tools → Master.
- **Bungee** in flash bank 2 (3.1.B, `..`).
- **Clip Deck** (3.1.E, `../../3.1.9-clip-deck`): Start / End / Loop, 8 hot cues, the waveform editor.
- **Warp Stutter / Fill / Repitch / Chop / Grain / Bungee.**
- **The preset-folder fixes** (`docs/preset-folders.md`):
  - no dropout when entering big folders;
  - Save / Save As safe in deep folders;
  - a failed save is shown, not lost.

The menu version reads `3.1.T`. Built with `build_patch.py --lineup`.

## Install

1. Put `BLACKBOX.BIN` and `BBWARP.BIN` from this folder in the card root. `BBWARP.BIN` takes the place of `BBBUNGEE.BIN`.
2. Flash `BLACKBOX.BIN` the usual way (BACK + INFO at power-on).
3. On first boot the loader programs bank 2 from `BBWARP.BIN` and restarts the unit.
   - A unit that has run 3.1.R or 3.1.B needs no `RBTAKE.TXT`.
   - Going back to 3.1.B later programs bank 2 again from `BBBUNGEE.BIN`.
4. Presets saved on 3.1.B load with their old Warp numbers, which mean different modes here:
   - Bungee was 1 (2 in the Repitch build), which is now Fill (or Repitch).
   - Set Warp again on those pads.
5. Presets saved on 3.1.E keep their Start / End / Loop and cues: they're stored by name, and only the internal ids moved.

## The modes

Clip → Pos → **Warp**. A Slicer pad always plays stock grains, except with Repitch.

| Warp | What it does | Where |
| --- | --- | --- |
| **Stutter** | Stock Stretch: one grain per Sync unit at 1x. When the tempo is slower, a grain runs on into the next segment, which then plays that audio again (the stutter). With **Sync = Slice** it does the same with the slices as its segments (stock gates the gap there instead). | stock; on slices bank 2, per lane |
| **Fill** | Segments start on the Sync grid, or at the slices with Sync = Slice, and play at 1x. When the tempo is slower, the gap until the next segment is filled by looping the segment's tail. The loop length is matched on the waveform, with a 1024-frame crossfade, and grows with the gap so it repeats about twice at most (see below). When the tempo is faster, a segment is cut by the next one with a 512-frame fade. | bank 2, per lane |
| **Repitch** | Varispeed: the grain rate is scaled by the tempo ratio, so pitch moves with tempo. | stock path, scaled |
| **Chop** | Fill's segments, but each segment fades out (1024 frames) where its audio ends. A slower tempo leaves a gap, a faster one cuts. It's a sliced-loop player that retriggers on the grid or the slices. | bank 2, per lane |
| **Grain** | The stock Granular engine's model, synced to the clock: the playhead scans the file at the tempo ratio. The settings are fixed (see below). With Sync on, the playhead snaps to the clock as the clock reaches each segment. Grains are centred on the playhead. | bank 2, per lane |
| **Bungee** | 3.1.B's phase vocoder: one Clip at a time, the latest launch wins, offered only within one choke group (`../README.md`). | bank 2, own task |

Grain's settings are the Warp Lab's for pads and melodic material: Size 200, Density 1000, Scatter 9, Window 100, Pan Rnd 300, Root none.

How the modes follow the Sync parameter:
- **Sync on:** the segments are units of that note length. A very long grid is thinned to 2048 starts.
- **Sync = Slice:** the segments are the pad's slices.
- **Sync None:** the whole file is one segment.

Fill, Chop and Grain follow the clock while the transport runs: the launch's tick, the tempo map, and the Clip's length in ticks, as Bungee's clock lock does. With the transport stopped they follow the tempo ratio instead.

**Unlike Bungee, Fill, Chop and Grain are not one voice.** Each lane has its own engine, so any number of pads can use them, and a choke group behaves as stock. Up to 8 lanes render at once; a 9th plays stock grains.

**Around the engines** they behave like a stock lane (`tools/stretch/bungee/lane.h`):
- the pad's Level, with velocity and mods, and pan;
- the amp envelope, the pad filter;
- the pad view's playing feedback and playhead;
- STOP, mute, release, and Loop off / Trigger ending at the end of the file.

If bank 2 isn't running (`Bungee!` on the list), Fill, Chop, Grain and Stutter on slices play stock grains.

**Chop** was called Slice in the first 3.1.T build. Its Warp value is still 3, so presets keep it.

## Fixed after the first test on the unit

Measured on the bench at 90 BPM on a 130 BPM Clip (0.69x), with a click at every 1/16 unit; the `Lineup:` timing cases run the same checks.

- **Grain didn't start after a quantised launch.** With a launch delay longer than one block, the grains' note-on never reached the engine, so the lane stayed silent. Now it goes with the first block that plays.
- **Grain was early.** Each grain started reading where the playhead was when the grain began, so at a slower tempo its middle read ahead of the playhead: hits came about 40 ms early at 0.69x (later at a faster tempo). Grains are now centred on the playhead, so hits land within ~1 ms of the clock on average. Window 100's random position still spreads them about ±6 ms, and Size 200's 210 ms grains smear transients: that is the setting, not timing.
- **Grain at a Clip Deck Start or cue:** the first grains read from the file's start. They read from the unit now.
- **Fill broke up when the clock wobbled.** Any step back of the clock's position, even a frame, restarted the segment playing, and a restart every few blocks is a buzz. On the bench a ±3-frame jitter was enough. Now only a drop of more than half the loop is a wrap; small steps keep the segment, and the clock only moves Fill's position once it is off by more than 64 frames.
- **Fill's loop search** cost ~490k instructions in the one block where a segment ran out (enough to overrun with a few pads on the same grid). It is now spread over the segment: at most ~140k per block, the same loop picked.
- **Fill's loop is longer at slow tempos.** At 1/16 on a 130 BPM Clip below ~87 BPM the gap is longer than the longest loop the Lab allows (half a segment), so the loop repeats, and a short loop repeated sounds like a tone. On the device the loop now goes from half the gap up to 3/4 of the segment (up to 0.5 s for a coarse Sync), so it repeats about twice at most. The Warp Lab keeps its 100 ms loops.
- **Bungee was ahead after every launch.** The raw Clip plays its first 43 ms at 1x, so when Bungee took over its clicks came up to ~18 ms early at 0.69x (more at slower tempos), and the clock lock took about a second to pull it back. With Clip Deck that happened at every loop wrap and cue jump. Bungee now starts that much further back, so it is within ~1 ms of the clock from the first unit (55, 90 and 120 BPM measured).
- **Bungee re-launched at the file's own wrap** under Clip Deck (last unit → first). That is straight on now.
- **Bungee below half the Clip's tempo** (65 BPM on a 130 BPM Clip) ran at 0.5x with its clock lock off and drifted. Its range is now 0.25x–2x, as Fill, Chop and Grain.
- **Stutter with Sync = Slice** now runs on like stock Stretch does on the grid, with the slices as segments. Stock gates the gap there.

## Clip Deck in this build

Clip Deck works as on 3.1.E (`../../3.1.9-clip-deck/README.md`), on **every** Warp mode, wherever the Clip is on a note-value Sync grid (as on 3.1.E: not with Sync None or Sync = Slice).

- **Stutter and Repitch** are stock's grains, so Clip Deck steers them as it does on 3.1.E.
- **Fill, Chop, Grain and Bungee** render in bank 2, without stock's lane render. A sequencer (`tools/stretch/bungee/lane.h`, `DeckSeq`) calls Clip Deck's own `deck_limit` / `deck_wrap_unit` at each unit, as the lane render does. It covers:
  - Start, or a cue launch;
  - Loop End → Loop Start;
  - End with Loop off / Trigger;
  - a pending cue jump on the next Quant boundary;
  - the grid offset.
- **Fill / Chop:** they play a virtual file of the next few units, so their segments, pre-roll and tail loop land on the right audio after a wrap or a jump. The next unit is decided ~320 frames (~7 ms) before it starts, for the pre-roll, so a cue pressed in those last ms takes the next Quant boundary.
- **Grain:** the playhead follows the sequence.
- **Bungee:** a loop wrap or a jump re-launches at the target on the unit it happens, the instant way a locate does: the raw Clip first, then Bungee 43 ms later. The file's own wrap (last unit → first) is straight on.
  - Fixed on the way, in 3.1.B too: after a re-launch to an earlier frame, Bungee's first output chunk still came from before the reset. It was taken for the new launch, and a second re-launch followed.

What changes against 3.1.E:
- **Param ids:** Clip Deck's ids move from `0x19D`–`0x1AC` to `0x1B0`–`0x1BF`, past the Mix line's own (`0x19D`–`0x1A9`: Seq Rec, Env Trig, Master). Presets store them by name.
- **Slices:** 3.1.Y's 2048 slices are not in this build. Their tables would sit in the RAM bank 2 uses, so stock's 128 slices apply.
- **Version text:** "3.1.T" goes after the cave, as on 3.1.E (`tools/version_text.py`). The '9' of "3.1.9" stays, so the naming keyboard's 9 key types 9. 3.1.Q and 3.1.B overwrite it.
- **Next-grain hook:** the call at `0x08066CF2` belongs to both Clip Deck and the Mix line's Env Trig. `hook_wrap_grain` chooses the unit, then jumps on to `env_slice`, which starts the grain.

## Bungee in this build (and in 3.1.B)

- **Volume:** the pad's Level, with velocity and its mods, and pan now apply to Bungee, as stock's grain reader applies them.
- **Pitch:** the Clip's Pitch, plus pitch mods, now works on Bungee, from **−12 to +7 semitones**, at the same tempo.
  - **How:** Bungee renders at speed ÷ pitch and the audio side reads its ring that much faster or slower, with cubic interpolation.
  - **Memory:** Bungee's own resampling would need ~65–80 KB more internal RAM, which isn't there. This needs none.
  - **CPU:** pitch up costs Bungee's task proportionally more, about 1.5× at +7 st. That's why the range stops there.
- **Launch timing and range** (the fixes above, in `bungee.cpp`): on the beat from the launch, and 0.25x–2x. 3.1.B's own binaries in `..` are not rebuilt with them yet.
- **Pitched launch:** a launch at a pitch other than 0 plays no raw Clip first, because it would be at the wrong pitch for the crossfade. It starts ~43 ms later, then the clock lock pulls it onto the beat.

## Memory

- **SDRAM pool:** Fill, Chop and Grain keep their state there, about 41 KB. `rb_pair` takes it from stock's SDRAM allocator once at boot, while the app is built (`RbExt.pool_bytes` in `tools/stretch/rubber/rubber.h`). It holds:
  - 8 lane engines;
  - the loop-match scratch;
  - the segment table.
- **Bank 2's internal RAM:** unchanged from 3.1.B (+8 bytes). Bungee's heap still fits.
- **Granular's tables:** on the device it reads stock's Hann table (`0x080D26AC`) and computes its pan gains.

## Build and bench

```bash
.venv/bin/python firmware/patches/3.1.9-mix-bungee/build_patch.py --lineup          # lineup/
.venv/bin/python firmware/patches/3.1.9-mix-bungee/build_patch.py --lineup --emu    # bench build
.venv/bin/python tools/bench/run_tests.py --image tools/stretch/bungee/build-lineup/BLACKBOX_emu.BIN
```

`--lineup` builds in `tools/stretch/bungee/build-lineup/`, so it never shares objects with 3.1.B. The `Lineup:` bench cases cover:
- the list;
- the tempo ratio per Warp;
- Fill's tail loop and Chop's gaps on the grid and at slices, locked to the clock;
- Grain, also after a long quantise delay, and where its hits land against the clock;
- Fill through a jittery clock at a slow tempo, and its CPU per block;
- Level, loop off;
- Stutter on slices (run-on), and on a grid left to stock;
- Bungee's Pitch and Level.

## Not done yet

- **Pitch on Fill / Chop / Grain:** the Pitch knob doesn't change them; their heads play at 1x.
