# Bungee Basic M7 cost — vs Rubber Band R2

Budget: **2.56M cycles** per 256-frame block @ 480 MHz (same harness and cost model as
`tools/lab/rb/m7/RESULTS.md`: Unicorn, 1 cycle/insn except unpipelined FPU ops; no dual
issue, no cache/SDRAM stalls).

Build: Bungee Basic v2.4.35, float only, `-O2 -fno-exceptions -fno-rtti`,
`EIGEN_DONT_PARALLELIZE`, `BUNGEE_SELF_TEST=0`, FPv5-SP. pffft is swapped for CMSIS-DSP
`arm_rfft_fast_f32` (`m7/Fourier_cmsis.cpp` replaces `src/Fourier.cpp`; Bungee's largest
transform at 48 kHz is 4096, which CMSIS's rfft_fast still covers). Driver `m7/bgm7.cpp` is
`../bungee.cpp`'s grain loop (looping clip, `speed = ratio`, `resampleMode_autoOut`, preroll),
48 kHz stereo, 256-frame output blocks.

Test: `rb/m7/test_loop.wav` (1 s stereo 48 kHz), `BLOCKS=40`, first 8 blocks skipped.

## Results

| Build | Ratio | Insns/block | Cycles/block (avg) | % avg | Peak block | Heap | Text |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Bungee, CMSIS FFT (default 512 hop) | 0.8 | 1.12M | **1.17M** | 45.9% | 2.35M (92.0%) | 516 KB | 193 KB |
| | 0.6 | 1.12M | **1.17M** | 45.9% | 2.36M (92.3%) | 516 KB | |
| | 1.25 | 1.11M | **1.17M** | 45.8% | 2.37M (92.6%) | 516 KB | |
| Bungee, CMSIS FFT, `HOP=-1` (256 hop) | 0.8 | 1.09M | **1.15M** | 44.7% | 1.15M (45.0%) | 268 KB | 193 KB |
| | 0.6 | 1.09M | **1.14M** | 44.7% | 1.15M (44.9%) | 268 KB | |
| | 1.25 | 1.09M | **1.15M** | 44.8% | 1.15M (45.0%) | 268 KB | |
| **Bungee, CMSIS FFT, `SPLIT=1` (512 hop)** | 0.8 | 1.11M | **1.18M** | 45.9% | 1.30M (50.9%) | 516 KB | 198 KB |
| | 0.6 | 1.11M | **1.18M** | 45.9% | 1.30M (50.9%) | 516 KB | |
| | 1.25 | 1.11M | **1.18M** | 45.9% | 1.31M (51.0%) | 516 KB | |
| Bungee, scalar pffft (reference) | 0.8 | 1.04M | 1.10M | 43.0% | 2.21M (86.3%) | 516 KB | 127 KB |
| **RB R2 optimized** (rb/m7) | 0.8 / 0.6 / 1.25 | 1.09–1.19M | **1.42–1.55M** | 55–60% | ~1.55M (60.6%) | ~7 MB | ~639 KB |

Text sizes are whole-ELF `.text` (Bungee + Eigen + libm/libstdc++ + CMSIS tables for every
size; trimming the tables to 4096 would shrink the CMSIS build toward the pffft one).

**Cost doesn't depend on ratio.** Bungee works at a fixed output (synthesis) hop: one grain
per 512 output frames by default, so work arrives in bursts. One block pays for a whole
grain (~2.35M) and the next pays ~0. With `log2SynthesisHopAdjust = -1` there's one grain
per block, at half the size, so the load is flat at ~1.15M every block. Bungee's header warns
that a non-zero adjustment "is likely to result in degraded audio". That needs a listen in
the lab before anyone relies on it.

**Correctness:** `rb/m7/compare.py probe_bungee_pffft.elf probe_bungee.elf` gives gain 1.000,
SNR 95 dB (0.8) / 77 dB (1.25) at lag 0. The CMSIS backend reproduces stock Bungee.

**Profile** (default hop, ratio 0.8, `PROFILE=1`):
- CMSIS FFT ~42%: radix8 butterfly 19%, rfft 9%, cfft 9%, bitreversal 6%.
- libm transcendentals ~25%: atanf / atan2f / rem_pio2f / sinf / cosf / expf / cexpf.
- Bungee's own grain code ~15%: synthesise, analyse, windows.

In this cost model CMSIS isn't faster than scalar pffft. The extra input copies and the
inverse ×N rescale cancel out what the FFT itself saves. Fast atan2/sincos/exp approximations,
like `USE_APPROXIMATE_ATAN2` / `USE_FAST_PHASOR` in the RB build, are the obvious lever:
they'd probably bring it down to ~0.85–0.9M per block. That's an estimate and hasn't been
measured.

**Split grains (`SPLIT=1`, chosen for the device):** the default 512 hop keeps its sound but
moves the work so no block runs a whole grain. Each block hands out its 256 frames, then does
one half of the next grain: `specifyGrain` + input copy + `analyseGrain` in one block,
`synthesiseGrain` + `next` in the block that would otherwise run dry. Per-block cost
alternates ~1.05M / ~1.30M, so the peak is **51%** instead of 92%, with the same average and
no added latency. Output is bit-identical to the unsplit build
(`compare.py probe_bungee.elf probe_bungee_split.elf`: SNR 228 dB, same start delay).

## Verdict vs RB R2

**GO, as a candidate to replace RB, with one condition: the per-grain burst.**

- **CPU:** on average ~1.17M vs 1.42–1.55M, so ~20–25% cheaper. That's not the ~2× the wasm
  lab showed, because RB's M7 build already has CMSIS FFT plus fast trig and Bungee's
  doesn't yet. At the default hop the *peak* is worse than RB (92% vs 61%) unless the grain is
  rendered outside the audio interrupt, one block ahead. That costs +256 frames of latency
  but averages out to ~46%. The 256 hop gets a flat 45% without that, pending a quality check.
- **Memory:** ~516 KB heap (268 KB at 256 hop) vs ~7 MB. Bungee's working set can live in
  internal SRAM, where RB's has to sit in SDRAM. The cost model ignores memory stalls, so
  real hardware should favour Bungee *more* than these numbers show.
- **Code:** ~193 KB vs ~639 KB, which fits bank 2 easily.
- **License:** MPL-2.0 vs GPL.

## Reproduce

```sh
cd tools/lab/bungee/m7
./build.sh               # probe_bungee.elf
HOP=-1 ./build.sh        # probe_bungee_h256.elf
SPLIT=1 ./build.sh       # probe_bungee_split.elf
FFT=pffft ./build.sh     # probe_bungee_pffft.elf
cd ../../rb/m7
BLOCKS=40 RATIOS=0.8,0.6,1.25 LABEL=bungee PERBLOCK=1 PROFILE=1 \
  .venv/bin/python measure.py ../../bungee/m7/probe_bungee.elf test_loop.wav 0
.venv/bin/python compare.py ../../bungee/m7/probe_bungee_pffft.elf ../../bungee/m7/probe_bungee.elf test_loop.wav 0 0.8 60
```

`measure.py` gained `LABEL` (row name) and `PERBLOCK` (dump per-block cycles) env options for this.
