# 3.1.B Warp Bungee (Bungee in flash bank 2)

3.1.Q (folders, new Mix, Tools → Master, looper) with **Warp: Stretch / Bungee**. Bungee renders one Clip at a time through [Bungee Basic](https://github.com/bungee-audio-stretch/bungee) v2.4.35, a phase-vocoder stretcher, at its default 512-frame hop. The menu version reads `3.1.B`.

This is 3.1.R (Warp Rubber, `../3.1.9-mix-rubber`) with Bungee in place of Rubber Band. Everything around the stretcher is 3.1.R's and unchanged:

- the bank-2 ABI (`tools/stretch/rubber/rubber.h`)
- pairing and the card loader, including `RBTAKE.TXT` / `RBFREE.TXT` (`tools/bank2`)
- the crash recorder and CPU meter (`tools/diag`)
- the CPU guard, the own stack, ITCM for hot code, and SRAM-only memory

The claim word is the same too, so a unit that has run 3.1.R treats bank 2 as its own and switches between the two builds without a takeover file.

## Install

1. Put `BLACKBOX.BIN` and `BBBUNGEE.BIN` from this folder in the card root.
2. **If bank 2 holds blackbox-mod's payload** (Bungee's label then ends in ` x`), also put an empty `RBTAKE.TXT` in the card root. This lets the loader erase bank 2 sectors 0–6 and install Bungee. `RBFREE.TXT` gives bank 2 back later. See 3.1.R's README for both files.
3. Flash `BLACKBOX.BIN` the usual way (BACK + INFO at power-on).
4. On first boot the loader checks `BBBUNGEE.BIN`, programs bank 2 and restarts the unit.

## Using it

- Clip → Pos → **Warp: Bungee**. Slicer Clips keep stock grains.
- The tempo ratio is clamped to **0.5×–2×** of the Clip's own tempo. The cost per block is the same at every ratio.
- **Launches are instant.** Bungee needs eight half-grain blocks (~43 ms) to prime. During that time the Clip plays unstretched from frame 0, then hands over to Bungee at frame 2048 with a 128-frame crossfade. Bungee primes from that frame with its first hop at speed 1, so both signals are in phase at the handoff. The first 43 ms plays at the Clip's own tempo, ≤ 11 ms off the target.
- One Clip at a time renders through Bungee. The first one playing owns it, and a second plays stock Stretch grains until the first stops.
- A mono Clip runs on a one-channel stretcher (~60% of the stereo cost). The launch that switches between mono and stereo rebuilds the stretcher; that launch plays the Clip raw for one block longer.

The Warp list label reports Bungee's state, as 3.1.R's does for Rubber:

| Label | Meaning |
| --- | --- |
| `Bungee` | paired and running |
| `B74 r22a20s28` | running, with where its time goes over the last second, as % of the block: total, clip reads, analysis, synthesis (`BG_PROFILE`, on in this build) |
| `Bungee 46%` | the same, total only (`BG_PROFILE=0`) |
| `Bungee..` | paired, starting |
| `Bungee!` | bank 2 is empty, damaged or from another build, so stock grains play |
| `Bungee! x` / `-` / `?` / `t` / `e` / `v` | the loader didn't write bank 2: not ours (needs `RBTAKE.TXT`), no `BBBUNGEE.BIN`, wrong or damaged file, tries used up, flash error, didn't read back |
| `Bungee! mem` | the stretcher didn't fit its memory (bench says it does) |
| `Bungee! cpu` | switched itself off before the watchdog: its smoothed cost over 75% of the block, or the whole audio task over 95% (Tools load), for 32 blocks |
| `Bungee!! ` / `Bungee! 11` … | the boot before died starting it, or at that stage (see 3.1.R) |

## How the stretcher runs

- **Split grains.** Each grain is spread over two 256-frame blocks: `specifyGrain` + `analyseGrain` in one, `synthesiseGrain` in the next. No block pays for a whole grain. On the M7 model that gives a 46% average and 51% peak, against 92% peak unsplit, with bit-identical output (`tools/lab/bungee/M7_RESULTS.md` on `experimental/rubberband`).
- **Fixed pitch.** Pitch stays 1 and in/out rates are equal, so Bungee's resampling buffers are never allocated (`bungee-blackbox.patch`, `BUNGEE_FIXED_PITCH`).
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
| Leaf stack | SRAM3 `0x30045000`–`0x30048000` | 12 KB |

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
