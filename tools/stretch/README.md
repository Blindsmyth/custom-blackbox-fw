# Stretch prototypes (host + cave)

Shared C for Clip Warp modes. Prototype and listen on the Mac first; flash only after the WAV sounds right.

| File | Role |
| --- | --- |
| `smooth.h` / `smooth.c` | WSOLA: window 2048, hop 1024, search ±160 (16→4→1, early-out SAD), flash Hann LUT (`repitch="2"`) |
| `hann_lut.h` | Precomputed Smooth window (regenerate: `python3 -c` in git history / see gen note) |
| `smooth_fw.c` | Cave hook for Smooth: stock scratch + envelope + accumulate |
| `snap.h` / `snap.c` | Slice-guided hops + XF (`repitch="3"`); needs Sync-to-Slice markers |
| `snap_fw.c` | Cave hook for Snap |
| `vari.h` / `vari.c` | VariPhrase-inspired: epoch/onset map + map-driven XF grains (`repitch="4"`) |
| `vari_fw.c` | Cave hook for Vari; map at SRAM4 `0x3800E800` |
| `proto.c` | Stretch a real WAV offline (same block size as the device) |
| `compare.c` | Synthetic Smooth A/B regression (no input file) |

Warp Grain (`repitch="5"`) stays on stock `Clip_StartGrain` with window 2048. Host A/B: `--mode grain` vs stock Stretch `--mode naive` (256/128).

## Offline prototype

```
clang -O2 -o tools/stretch/proto tools/stretch/proto.c \
    tools/stretch/smooth.c tools/stretch/snap.c tools/stretch/vari.c -lm
mkdir -p tools/stretch/out

# Same loop you use on the pad (16-bit PCM wav). Ratio > 1 = faster tempo / shorter output.
tools/stretch/proto --mode smooth --in /path/to/loop.wav --ratio 1.25 --out tools/stretch/out/smooth.wav
tools/stretch/proto --mode snap --in /path/to/loop.wav --slices 0,12000,24000 --ratio 1.25 --out tools/stretch/out/snap.wav
tools/stretch/proto --mode vari --in /path/to/loop.wav --ratio 1.25 --out tools/stretch/out/vari.wav --marks tools/stretch/out/vari_marks.txt
tools/stretch/proto --mode grain --in /path/to/loop.wav --ratio 1.25 --out tools/stretch/out/grain.wav
tools/stretch/proto --mode naive --in /path/to/loop.wav --ratio 1.25 --out tools/stretch/out/naive.wav

afplay tools/stretch/out/grain.wav
```

Renders in **1024-sample blocks** (Blackbox DMA half) so join bugs show up here the same way as on device. Prints a `crackle/k` score (large sample-to-sample jumps per 1000 samples): hop-edge clicks raise it; steady smear does not.

### Smooth cost

Per 1024-sample block, counted in Unicorn against the cave build (stereo, ratio 1.25, PCM reads stubbed):

| Build | Mean | Peak hop |
| --- | --- | --- |
| 3.1.Z Smooth | 315k insns | 418k |
| 3.1.S Smooth | 103k insns | 137k |

The search was ~52% and the per-sample `window_at` OLA ~35%. 3.1.S keeps the algorithm: the SAD stops a candidate once its partial sum reaches the best score, runs four lanes with a compile-time stride, refines 16→4→1 (15 candidates instead of 33), and the accumulator is split into an output hop and a tail so there is no shift or zeroing. Host `crackle/k` stays within ±5% of 3.1.Z.

Guiding Smooth from the Vari map was tried on paper and dropped: one `vari_analyze_step` (budget 4) is ~3.4M insns, about 25× a whole Smooth hop, and Vari's epochs are a period grid from sample 0, not waveform-anchored, so snapping to them does not align grains.

### VariPhrase mode

`vari_analyze` builds ascending splice marks from local AMDF periods (~80–400 Hz @ 48 kHz) plus strong onsets (caller provides `VARI_SCRATCH_N` floats — never on the device audio stack). On device, `vari_fw.c` steps a few frames per mix block. `vari_process` places period-centered hops on that map and crossfades joins. Voice ABI matches Smooth/Snap at `0x38007000`; epoch map at `0x3800E800`.

### Grain mode

OLA at win 2048 / hop 1024 (stock Stretch is 256/128). On device, Warp=Grain rewrites `grain+0x54`/`+0x5c`/`+0x58` after `Clip_StartGrain` and leaves the stock mixer — tempo sync unchanged.

## Device

Smooth/Snap/Vari/Grain are in the Warp caves via `firmware/patches/*/build_patch.py` and `tools/cave_ld.py`. Smooth/Snap/Vari voice: SRAM4 `0x38007000`. Grain uses no extra SRAM.
