# Smooth WSOLA (host + cave)

Shared C for Clip Warp value **Smooth** (`repitch="2"`). Prototype and listen on the Mac first; flash only after the WAV sounds right.

| File | Role |
| --- | --- |
| `smooth.h` / `smooth.c` | Window 2048, hop 1024, search ±160, flash Hann LUT |
| `hann_lut.h` | Precomputed window (regenerate: `python3 -c` in git history / see gen note) |
| `smooth_fw.c` | Cave hook: stock scratch + envelope + accumulate |
| `proto.c` | Stretch a real WAV offline (same block size as the device) |
| `compare.c` | Synthetic A/B regression (no input file) |

## Offline prototype

```
clang -O2 -o tools/stretch/proto tools/stretch/proto.c tools/stretch/smooth.c -lm
mkdir -p tools/stretch/out

# Same loop you use on the pad (16-bit PCM wav). Ratio > 1 = faster tempo / shorter output.
tools/stretch/proto --in /path/to/loop.wav --ratio 1.25 --out tools/stretch/out/smooth.wav
tools/stretch/proto --in /path/to/loop.wav --ratio 1.25 --naive --out tools/stretch/out/naive.wav

# Open both in a DAW or afplay and A/B.
afplay tools/stretch/out/smooth.wav
```

Renders in **1024-sample blocks** (Blackbox DMA half) so join bugs show up here the same way as on device. Prints a `crackle/k` score (large sample-to-sample jumps per 1000 samples): hop-edge clicks raise it; steady WSOLA smear does not.

## Device

Linked into the Warp caves by `firmware/patches/*/build_patch.py` via `tools/cave_ld.py`. Voice state: SRAM4 `0x38007000`. One stereo Clip at a time.
