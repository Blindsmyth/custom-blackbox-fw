# 3.1.R Warp Rubber (Rubber Band in flash bank 2)

3.1.Q (folders, new Mix, Tools → Master, looper) with a third Clip Warp mode: **Warp: Stretch / Repitch / Rubber**. Rubber renders one Clip at a time through Rubber Band R2 (the real-time engine Mixxx uses). It is meant for playing one stereo song render against the project tempo, with the XY FX on top. The menu version reads `3.1.R`.

Rubber Band is about 620 KB of code, far more than the ~56 KB left after the stock image in bank 1. It lives in flash bank 2 (`0x08100000`), which the stock SD installer never writes. A small loader in bank 1 programs it from the card.

## Install

1. Put both files from this folder in the card root: `BLACKBOX.BIN` and `BBRUBBER.BIN`.
2. Flash `BLACKBOX.BIN` the usual way (BACK + INFO at power-on).
3. On the first boot, the Blackbox finds bank 2 empty, checks `BBRUBBER.BIN` all the way through, programs bank 2 (a few seconds) and restarts itself.
4. After that `BBRUBBER.BIN` can stay on the card or be removed: bank 2 is only written when it doesn't match the bank-1 image.

Every build pairs one `BLACKBOX.BIN` with one `BBRUBBER.BIN` (build id + sum). Mixing files from different builds just means bank 2 is reprogrammed from the card, or Rubber stays off if the card has the wrong one.

## Using it

- Clip → Pos → **Warp: Rubber**. Slicer Clips keep stock grains.
- The tempo ratio is clamped to **0.5×–1.25×** of the Clip's own tempo; beyond that it plays at the clamp.
- A launch or retrigger is silent for about 60 ms while the stretcher primes (spread over several blocks so the CPU never spikes).
- Only one Clip renders through Rubber at a time. The first one playing owns it; a second Rubber Clip plays stock Stretch grains until the first stops.

The Warp list shows Rubber's state in its label:

| Label | Meaning |
| --- | --- |
| `Rubber` | Bank 2 paired and running |
| `Rubber 58%` | Same, with the last second's Rubber CPU share of the audio block (after one second of playing) |
| `Rubber!` | Bank 2 is empty, damaged or from another build: Rubber plays stock Stretch grains |
| `Rubber!!` | The previous boot never came back from starting bank 2. Rubber stays off until a new `BBRUBBER.BIN` is programmed |

## Safety

- The loader only uses bank 2's flash registers (`KEYR2`/`CR2`/`SR2`/`CCR2`), only erases sectors 0–5, and writes a claim word at the start of sector 6. Sector 7 (`0x081E0000`, factory calibration) is never touched.
- Bank 2 is only erased when it is blank or carries our claim. Anything else there is left alone (Rubber then shows `Rubber!`).
- The whole file is summed before anything is erased. The header is written last, so a cut-off write reads as "no payload".
- At most two programming attempts per payload (counted in backup SRAM). A boot that faults reading bank 2 leaves a mark, so the next boot keeps off it.
- Bank 1 never calls into bank 2 until the pairing check passed. **Recovery:** flash stock `firmware/bins/3.1.9/BLACKBOX.bin`; bank 2 is then simply unused.

## Memory

- Rubber Band's heap: the top 1 MB of SDRAM (`0xC3F00000`–`0xC4000000`). The stock sample stack starts below it (patched `0xC4000000` → `0xC3F00000` at `0x08044498` and in `.data` at `0x080F1E08`), so samples get 1 MB less.
- Bank 2's `.data`/`.bss`: SRAM1 (`0x30000000`).
- Voice state and label: SRAM4 `0x38007000` (`RbVoice`, `tools/stretch/rubber/rubber.h`).

## Build

```bash
tools/stretch/rubber/fetch_rubberband.sh
.venv/bin/python firmware/patches/3.1.9-mix-rubber/build_patch.py
```

`--emu` builds a single-precision-FPU variant for the bench (`tools/stretch/rubber/build/*_emu.BIN`); Unicorn's M7 has no double FPU. Bench:

```bash
.venv/bin/python tools/bench/run_tests.py --image tools/stretch/rubber/build/BLACKBOX_emu.BIN --only Rubber
```

Sources: `tools/stretch/rubber/` (glue, runtime, CMSIS FFT, fast trig, Rubber Band patch), `tools/bank2/` (pairing, loader), `firmware/patches/3.1.9-folders-repitch/repitch.S` (`WARP_RUBBER` blocks).

## Licence

Rubber Band Library is GPL-2.0-or-later. A `BBRUBBER.BIN` (and the firmware pair) contains it and can only be passed on under the GPL, with source. CMSIS-DSP is Apache-2.0.
