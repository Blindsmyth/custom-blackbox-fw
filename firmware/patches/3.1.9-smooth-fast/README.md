# 3.1.S faster Smooth on 3.1.Z

Same image as [../3.1.9-folders-repitch](../3.1.9-folders-repitch/README.md) (folders, Warp incl. Vari, 2048 slices) with a cheaper Warp **Smooth**. The menu version reads `3.1.S`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged.

`build_patch.py` reuses the 3.1.Z build script: same cave sources and hook sites, different outputs and version letter. Smooth lives in `tools/stretch/smooth.c`.

## What changed in Smooth

Same WSOLA (window 2048, hop 1024, search ±160, Hann OLA, 128-sample XF on hard hits). Per 1024-sample block it now runs ~103k instructions (peak hop 137k) instead of ~315k (peak 418k). Details and numbers: [tools/stretch/README.md](../../../tools/stretch/README.md#smooth-cost).

The only audible-in-principle change is the 16→4→1 refine (host `crackle/k` within ±5% of 3.1.Z, some better, some worse). Everything else is the same output to the LSB.

## Voice ownership fix

Smooth/Snap/Vari share one voice. The owning clip only released it from its own lane-0 call with both lanes idle, so if it stopped rendering first (Warp changed, preset reload, clip freed) the slot stayed taken and every Smooth pad quietly played stock Stretch grains, even across a soft reset (SRAM4 survives it). `hook_lane` in `../3.1.9-folders-repitch/repitch.S` now takes the voice once the owner has missed 3 lane-0 calls (counter at `0x38005110`). Two Smooth pads playing at once still leave the second on stock grains.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.S`.

## Check on device

1. Warp Smooth on a Clip should now sound different from Stretch right away (no cold boot needed).
2. One Clip on Smooth at a tempo offset, where 3.1.Z struggled. Listen for dropouts.
3. Same pad on 3.1.Z vs 3.1.S at 0.75 / 1.0 / 1.25: kicks and tonal material should sound the same.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-smooth-fast/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-smooth-fast/BLACKBOX.BIN
```
