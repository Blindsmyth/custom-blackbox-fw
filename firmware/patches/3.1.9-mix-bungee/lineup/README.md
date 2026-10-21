# 3.1.T Warp lineup

3.1.B (`..`, Bungee in flash bank 2) with Warp **Stutter / Fill / Repitch / Slice / Grain / Bungee**. The menu version reads `3.1.T`. Built with `build_patch.py --lineup`.

## Install

1. Put `BLACKBOX.BIN` and `BBWARP.BIN` from this folder in the card root. `BBWARP.BIN` takes the place of `BBBUNGEE.BIN`.
2. Flash `BLACKBOX.BIN` the usual way (BACK + INFO at power-on).
3. On first boot the loader programs bank 2 from `BBWARP.BIN` and restarts the unit.
   - A unit that has run 3.1.R or 3.1.B needs no `RBTAKE.TXT`.
   - Going back to 3.1.B later programs bank 2 again from `BBBUNGEE.BIN`.
4. Presets saved on 3.1.B load with their old Warp numbers, which mean different modes here:
   - Bungee was 1 (2 in the Repitch build), which is now Fill (or Repitch).
   - Set Warp again on those pads.

## The modes

Clip → Pos → **Warp**. A Slicer pad always plays stock grains, except with Repitch.

| Warp | What it does | Where |
| --- | --- | --- |
| **Stutter** | Stock Stretch: one grain per Sync unit at 1x. When the tempo is slower, a grain runs on into the next segment, which then plays that audio again (the stutter). | stock |
| **Fill** | Segments start on the Sync grid, or at the slices with Sync = Slice, and play at 1x. When the tempo is slower, the gap until the next segment is filled by looping the segment's tail. The loop length is matched on the waveform, with a 1024-frame crossfade. When the tempo is faster, a segment is cut by the next one with a 512-frame fade. | bank 2, per lane |
| **Repitch** | Varispeed: the grain rate is scaled by the tempo ratio, so pitch moves with tempo. | stock path, scaled |
| **Slice** | Fill's segments, but each segment fades out (1024 frames) where its audio ends. A slower tempo leaves a gap, a faster one cuts. It's a sliced-loop player that retriggers on the grid or the slices. | bank 2, per lane |
| **Grain** | The stock Granular engine's model, synced to the clock: the playhead scans the file at the tempo ratio. The settings are fixed (see below). With Sync on, the playhead lands on each segment's start as the clock reaches it. | bank 2, per lane |
| **Bungee** | 3.1.B's phase vocoder: one Clip at a time, the latest launch wins, offered only within one choke group (`../README.md`). | bank 2, own task |

Grain's settings are the Warp Lab's for pads and melodic material: Size 200, Density 1000, Scatter 9, Window 100, Pan Rnd 300, Root none.

How the modes follow the Sync parameter:
- **Sync on:** the segments are units of that note length. A very long grid is thinned to 2048 starts.
- **Sync = Slice:** the segments are the pad's slices.
- **Sync None:** the whole file is one segment.

Fill, Slice and Grain follow the clock while the transport runs: the launch's tick, the tempo map, and the Clip's length in ticks, as Bungee's clock lock does. With the transport stopped they follow the tempo ratio instead.

**Unlike Bungee, Fill, Slice and Grain are not one voice.** Each lane has its own engine, so any number of pads can use them, and a choke group behaves as stock. Up to 8 lanes render at once; a 9th plays stock grains.

**Around the engines** they behave like a stock lane (`tools/stretch/bungee/lane.h`):
- the pad's Level, with velocity and mods, and pan;
- the amp envelope, the pad filter;
- the pad view's playing feedback and playhead;
- STOP, mute, release, and Loop off / Trigger ending at the end of the file.

If bank 2 isn't running (`Bungee!` on the list), Fill, Slice and Grain play stock grains, like Stutter.

## Bungee in this build (and in 3.1.B)

- **Volume:** the pad's Level, with velocity and its mods, and pan now apply to Bungee, as stock's grain reader applies them.
- **Pitch:** the Clip's Pitch, plus pitch mods, now works on Bungee, from **−12 to +7 semitones**, at the same tempo.
  - **How:** Bungee renders at speed ÷ pitch and the audio side reads its ring that much faster or slower, with cubic interpolation.
  - **Memory:** Bungee's own resampling would need ~65–80 KB more internal RAM, which isn't there. This needs none.
  - **CPU:** pitch up costs Bungee's task proportionally more, about 1.5× at +7 st. That's why the range stops there.
- **Pitched launch:** a launch at a pitch other than 0 plays no raw Clip first, because it would be at the wrong pitch for the crossfade. It starts ~43 ms later, then the clock lock pulls it onto the beat.

## Memory

- **SDRAM pool:** Fill, Slice and Grain keep their state there, about 41 KB. `rb_pair` takes it from stock's SDRAM allocator once at boot, while the app is built (`RbExt.pool_bytes` in `tools/stretch/rubber/rubber.h`). It holds:
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
- Fill's tail loop and Slice's gaps on the grid and at slices, locked to the clock;
- Grain;
- Level, loop off;
- Stutter left to stock;
- Bungee's Pitch and Level.

## Not done yet

- **Pitch on Fill / Slice / Grain:** the Pitch knob doesn't change them; their heads play at 1x.
- **Clip Deck (3.1.E):** it's on this branch as its own build (`../../3.1.9-clip-deck`), not part of this image. Both builds use the folders/Warp cave, and Clip Deck keeps 3.1.Y's 2048 slices in the RAM bank 2 uses, so combining them needs that resolved first.
