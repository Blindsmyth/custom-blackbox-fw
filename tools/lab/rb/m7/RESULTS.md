# Rubber Band M7 cost — optimized R2 (CMSIS-DSP + fast trig)

Budget: **2.56M cycles** per 256-frame block @ 480 MHz.

## Baseline (unmodified, prior)

| Build | Cycles/block | % CPU |
| --- | --- | --- |
| R2 float, builtin FFT | ~5.2M | ~205% |

## Optimized (`OPT=cmsis ./build.sh` → `probe_cmsis.elf`)

Mixxx “faster”: `OptionProcessRealTime | OptionThreadingNever`, `PROCESS_SAMPLE_TYPE=float`,
`HAVE_CMSIS_DSP`, `USE_APPROXIMATE_ATAN2`, `USE_FAST_PHASOR`, `NO_EXCEPTIONS`.

Test: `test_loop.wav` (1 s stereo 48 kHz), `BLOCKS=40`.

| Ratio | Insns/block | Cycles/block | % of budget (avg) | Peak % | Heap |
| --- | --- | --- | --- | --- | --- |
| 0.8 | 1.19M | **1.55M** | **60.5%** | 60.5% | 7143 KB |
| 0.6 | 1.15M | **1.50M** | **58.5%** | 60.5% | 7363 KB |
| 1.25 | 1.09M | **1.42M** | **55.4%** | 60.6% | 7015 KB |

Code size (ELF text): ~639 KB (fits bank 2’s 896 KB).

### Gate (plan)

- Mean ≤ 1.5M cycles (~60%): **marginal pass** (1.42–1.55M; worst ratio 0.8 at 60.5%).
- p99 ≤ 2.3M (~90%): **pass** (peak ~60.6%).

**Verdict: GO for one Clip + idle UI/FX.** Expect little headroom; XY/FX under load may still glitch.
Profile (approx): `modifyChunk` 18%, CMSIS FFT butterfly+rfft ~25%, phase/`inversePolar` ~12%, atan2 ~6%.
A ~13% bucket attributed to `__is_ios_failure_handler` looks like profiler mis-tagging near C++ EH/typeinfo, not real audio work.

### Reproduce

```sh
cd tools/lab/rb/m7
OPT=cmsis ./build.sh
python3 -m venv .venv && .venv/bin/pip install unicorn capstone
BLOCKS=40 RATIOS=0.8,0.6,1.25 PROFILE=1 \
  .venv/bin/python measure.py probe_cmsis.elf test_loop.wav 0
```
