# 3.1.B Warp Bungee (Bungee in flash bank 2)

3.1.Q (folders, new Mix, Tools → Master, looper) with **Warp: Stretch / Bungee**. Bungee renders one Clip at a time through [Bungee Basic](https://github.com/bungee-audio-stretch/bungee) v2.4.35, a phase-vocoder stretcher, at its default 512-frame hop. The menu version reads `3.1.B`.

This is 3.1.R (Warp Rubber, `../3.1.9-mix-rubber`) with Bungee in place of Rubber Band. Everything around the stretcher is 3.1.R's and unchanged:

- the bank-2 ABI (`tools/stretch/rubber/rubber.h`)
- pairing and the card loader, including `RBTAKE.TXT` / `RBFREE.TXT` (`tools/bank2`)
- the crash recorder and CPU meter (`tools/diag`)
- the CPU guard, the own stack, ITCM for hot code, and SRAM-only memory

The claim word is the same too, so a unit that has run 3.1.R treats bank 2 as its own and switches between the two builds without a takeover file.

## Three builds

- **`BLACKBOX.BIN` + `BBBUNGEE.BIN`** here: Warp **Stretch / Bungee**.
- **`repitch/`** (`build_patch.py --with-repitch`): Warp **Stretch / Repitch / Bungee**, with Bungee at value 2. The bank-2 payload is the same.
- **`lineup/`** (`build_patch.py --lineup`): **3.1.T**, Warp **Stutter / Fill / Repitch / Slice / Grain / Bungee**. Its own payload, `BBWARP.BIN`. See [lineup/README.md](lineup/README.md).

## Install

1. Put `BLACKBOX.BIN` and `BBBUNGEE.BIN` from this folder in the card root.
2. **If bank 2 holds blackbox-mod's payload** (Bungee's label then ends in ` x`), also put an empty `RBTAKE.TXT` in the card root. This lets the loader erase bank 2 sectors 0–6 and install Bungee. `RBFREE.TXT` gives bank 2 back later. See 3.1.R's README for both files.
3. Flash `BLACKBOX.BIN` the usual way (BACK + INFO at power-on).
4. On first boot the loader checks `BBBUNGEE.BIN`, programs bank 2 and restarts the unit.

## Using it

- Clip → Pos → **Warp: Bungee**. Slicer Clips keep stock grains.
- The tempo ratio is clamped to **0.5×–2×** of the Clip's own tempo. The cost per block is the same at every ratio.
- **Launches are instant.** Bungee's first output comes out of its fourth grain. While its task primes, the Clip plays unstretched from frame 0, then hands over to Bungee at frame 2048 (~43 ms) with a 128-frame crossfade. If the task is late, the handoff waits and drops the frames it missed. Bungee primes from that frame with its first hop at speed 1, so both signals are in phase at the handoff. The first 43 ms plays at the Clip's own tempo, ≤ 11 ms off the target.
- One Clip at a time renders through Bungee, and **the latest launch takes it**. Switching between pads (a choke group over sections of one mixdown, say) has no gap: the new pad plays at once, and the old one ends as a choke's fast release would. Two Bungee voices at once don't fit: one stereo stretcher fills the leaf's internal RAM.
- **In time with the clock.** While the transport runs, each block compares Bungee's position in the file with where the clock says it should be. That's the transport's tempo map from the launch, 960 ticks a beat, against the Clip's length in beats. The speed is nudged by up to 2%; it's a tempo change, not pitch, so it's inaudible. The nudge is a PI loop (bench: 0.3% off settles to 2 frames in ~2 s). An error over 100 ms (a locate, an external clock jump) relaunches at the right place.
- **Loop off / Trigger:** the voice ends at the end of the file, as stock does.
- **One choke group.** Bungee is offered in the Warp list only on a pad that already has it, or in the same Excl Group as the pad(s) that do. On other pads the list just ends before Bungee (`tools/bank2/b2pair.c` `warp_count`). Switching between those pads is a choke switch, and the latest launch takes the voice. A preset saved before this rule can still have Bungee on pads in different groups; only one of them plays through Bungee at a time.
- **Sample loads that came out partial** (a fix to stock, both builds). Stock zero-fills a sample block when the card read fails, marks it as loaded anyway, and FatFs keeps the error on the file, so every later block of that sample also loads as zeros until you reload it. Now a failed block read clears that error, seeks back and retries (up to 3 times; `tools/diag/diag.c` `sd_read_retry`). The Tools bar shows ` E<retried>/<still failed>` once it has happened since boot.
- **Pad feedback:** the voice posts what stock's lane render posts, so the pads view and the waveform show it playing, with progress: the playhead message `0x4A` every block, and "started" (`0x1D`) when a quantised launch's delay is over.
- A mono Clip runs on a one-channel stretcher (~60% of the stereo cost). The launch that switches between mono and stereo rebuilds the stretcher, in the task.

The Warp list label reports Bungee's state, as 3.1.R's does for Rubber:

| Label | Meaning |
| --- | --- |
| `Bungee` | paired and running |
| `B40a19s19u0` | running: the Bungee task's share of the CPU over the last second, of which analysis and synthesis, and the audio blocks it was late for (`u`, played silent) (`BG_PROFILE`, on in this build) |
| `Bungee 46%` | the same, total only (`BG_PROFILE=0`) |
| `Bungee..` | paired, starting |
| `Bungee!` | bank 2 is empty, damaged or from another build, so stock grains play |
| `Bungee! x` / `-` / `?` / `t` / `e` / `v` | the loader didn't write bank 2: not ours (needs `RBTAKE.TXT`), no `BBBUNGEE.BIN`, wrong or damaged file, tries used up, flash error, didn't read back |
| `Bungee! mem` | the stretcher didn't fit its memory (bench says it does) |
| `Bungee! cpu` | switched itself off: the Bungee task late for more than an eighth of the blocks in a second, or (CPU guard) the audio side's smoothed cost over 75% of the block or the whole audio task over 95% for 32 blocks |
| `Bungee! task` | its background task could not be created (RTOS heap) |
| `Bungee!! ` / `Bungee! 11` … | the boot before died starting it, or at that stage (see 3.1.R) |

## How the stretcher runs

- **Its own task.** Bungee renders in its own FreeRTOS task at priority 5: below audio (6), above UI and disk (3) and USB host (4). It renders whole grains into a 2048-frame ring (~43 ms ahead), up to three grains per wake. Every audio block wakes it through a semaphore (`tools/bank2/b2pair.c` `rb_task`).
  - **What stays in the audio task:** the Clip's raw launch, copying from the ring, and the stock lane's envelope, filter and end of voice. That's cheap and flat (bench: peak/mean 1.00).
  - **Why:** on the unit Bungee took ~40% of every block in the audio task, and the firmware's own spikes on top overran blocks (crackle). Now Bungee's work no longer has to fit in the same 5.3 ms as everything else.
  - **Costs:** a tempo change reaches the sound up to ~43 ms later. If the task falls behind, those frames play silent and count as `u`; more than an eighth of a second's blocks switches Bungee off.
  - **Shared clip reads:** the stock reader isn't safe from two tasks (it updates the stream cache without a lock), so the task suspends the scheduler around each read.
  - **RTOS heap:** stock leaves only ~0.35 KB of it free, and the task needs ~0.7 KB, so the idle task's stack goes from 0x400 to 0x300 words (`0x08088C98`).
- **Fixed pitch inside Bungee.** Bungee itself runs at pitch 1 with equal in/out rates, so its resampling buffers are never allocated (`bungee-blackbox.patch`, `BUNGEE_FIXED_PITCH`).
- **The Clip's Pitch** (−12..+7 st, with pitch mods) is done outside Bungee: Bungee renders at speed ÷ pitch, and the audio side reads the ring pitch frames per output frame (cubic). That keeps the tempo, costs no RAM, and costs Bungee's task the pitch factor in CPU, hence the +7 st limit. A pitched launch has no raw-Clip start; it begins ~43 ms later.
- **The pad's Level and pan** (velocity and mods included) apply to Bungee as stock's grain reader applies them to grains (`lane.h`, shared with 3.1.T's lane modes).
- **FFT.** CMSIS-DSP `arm_rfft_fast_f32` (`tools/stretch/bungee/Fourier_cmsis.cpp`), with one shared scratch buffer.
- **Each clip frame read once.** The 4096-frame grain window slides: the frames it keeps move down and only the new hop is read. Before this, every frame went through the stock reader ~8 times.
- **Fast phase.** No libm per bin (`BUNGEE_FAST_PHASE`, `src/FastPhase.h` in the patch): a polynomial `atan2` that returns Bungee's int16 phase, and the phasor taken straight from the int16 rotation. Bench: the leaf's work per block went from 2.06M to 1.08M, and the output is 124 dB from the libm version.

The first unit run of 3.1.B (before these) showed 159% on Tools and then a watchdog reset. The old guard counted the leaf's raw blocks only, and needed 32 *in a row* over 75%. Bungee alternates a lighter and a heavier block, and the overload was the whole task, so the guard never fired. It now trips on the smoothed cost or on the whole audio task (`tools/bank2/b2pair.c`, `diag_load` in `tools/diag/diag.c`).

## Memory

All internal RAM, nothing in SDRAM: samples keep all of it, which is the lesson from 3.1.R's crash with full presets.

| What | Where | Size |
| --- | --- | --- |
| Bank 2 `.data`/`.bss` (FFT scratch, newlib) | SRAM1 `0x30000000` | ~20 KB |
| Bungee's heap, first pool (`rt.c`) | SRAM1–3 after `.bss`, up to `0x30045000` | 248 KB, 15 KB left (bench) |
| Heap, second pool: the synthesis buffers | ITCM after the hot code, up to `0x10000` | 33 KB, ~9 KB left (bench, emu build) |
| Hot code: CMSIS FFT, analysis/synthesis, grain timing, memcpy | ITCM `0x1000`+ | 19 KB |
| Grain input buffer (4096 × 2 floats) | SRAM4 `0x38008000`–`0x38010000` (free on this line) | 32 KB |
| Leaf stacks | SRAM3: Bungee task `0x30045000`–`0x30047000`, boot and audio side `0x30047000`–`0x30048000` | 8 + 4 KB (bench: 0.7 KB and 0.2 KB used) |
| Output ring | heap | 16 KB |

The heap allocator is 3.1.R's bump allocator plus a first-fit list. Bungee frees two window temporaries during setup and those get reused. Nothing is allocated while playing.

Trimming from Bungee's stock ~390 KB:
- **Resampling buffers:** −65 KB.
- **Per-kernel FFT scratch:** −64 KB; one 16 KB buffer is now shared.
- **Partial lists sized to the bins:** −48 KB. A partial spans at least two bins.
- **Input buffer:** −32 KB from SRAM1–3, moved to SRAM4.

## Build

```bash
tools/stretch/bungee/fetch_bungee.sh
.venv/bin/python firmware/patches/3.1.9-mix-bungee/build_patch.py
.venv/bin/python firmware/patches/3.1.9-mix-bungee/build_patch.py --emu      # bench build
.venv/bin/python tools/bench/run_tests.py --image tools/stretch/bungee/build/BLACKBOX_emu.BIN --only Rubber
```

The Rubber bench cases test whichever leaf the image was built with (`BBBUNGEE` or `BBRUBBER` payload beside it). They cover pairing, the lane hook, the loader against a flash model, takeover, the guard and diag. The engine case boots bank 2 and renders 60 blocks. It checks the launch is the Clip sample for sample, the pitch at 0.8×, and the heap fit.

## Licence

Bungee is MPL-2.0. `BBBUNGEE.BIN` contains it: its source files (with `bungee-blackbox.patch`) are available under the MPL, and the rest of the image isn't affected. CMSIS-DSP is Apache-2.0.
