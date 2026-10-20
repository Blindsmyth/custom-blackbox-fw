# 3.1.9 Bungee Warp (bank 2)

[Bungee Basic](https://github.com/bungee-audio-stretch/bungee) v2.4.35, the phase-vocoder stretcher, as Warp mode 6 (label **Bungee**), with the DSP leaf in **flash bank 2** in place of Rubber Band. It uses the default 512-frame hop, which sounded much less choppy than 256 in the Warp Lab.

**License:** Bungee is MPL-2.0. Its source files stay under MPL (changes to them must be published); the rest of the image is not affected.

## Cost (M7 model, one Clip)

| | Bungee (this) | RB R2 |
| --- | --- | --- |
| Avg / peak per 256-frame block | ~46% / ~51% | 55–60% / ~61% |
| Heap | ~0.5 MB of a 2 MB SDRAM arena | ~7 MB of 8 MB |
| Bank-2 payload | 116 KB | 627 KB |

Each grain is split across two blocks: `specifyGrain` + `analyseGrain` in one, `synthesiseGrain` in the next. This keeps the peak at 51% rather than 92% without changing the output. See [tools/lab/bungee/M7_RESULTS.md](../../../tools/lab/bungee/M7_RESULTS.md).

**Launches are instant.** Bungee only produces its first frame after its fourth grain (its output trails by two grains, plus a preroll grain). Priming at one half-grain per block takes 8 blocks (~43 ms). During that time the leaf plays the clip itself, unstretched and sample-exact, from frame 0. Meanwhile Bungee primes starting from clip frame T = 8 × block (2048 at 256 frames), which is where the raw playback will be when priming ends. At T the two meet, and a 128-frame linear crossfade hands over to Bungee.

The preroll hop runs at speed 1.0 and the clip's ratio applies from T on. This puts Bungee's phases at T in line with the raw clip: they match within ±4 frames, compared with 94–142 frames when the preroll runs at the clip's ratio. As a result the crossfade has no dip and no bump on sines, chords or the loop (`test_bank2.py`). The first ~43 ms plays at the clip's original tempo, which shifts it by ≤ 11 ms against the target tempo.

Build knobs (`DEFS=... ./build_bank2.sh`):
- `BG_XF` (crossfade length in frames)
- `BG_XF_POWER=1` (equal-power fade)
- `BG_PREROLL=1|0` (preroll at the clip's ratio, or no preroll)
- `PRIME=2` (prime in 4 blocks at ~94% CPU)

## Build

```sh
firmware/patches/3.1.9-bungee-warp/build_bank2.sh          # out/BBEXTRA.BIN + bg/bank2_syms.h
tools/lab/rb/m7/.venv/bin/python firmware/patches/3.1.9-bungee-warp/test_bank2.py   # Unicorn: PASS
tools/lab/rb/m7/.venv/bin/python firmware/patches/3.1.9-folders-repitch/build_patch.py
```

`build_patch.py` builds the Bungee leaf by default. `WARP_LEAF=rb` builds the old RB leaf instead (run `3.1.9-rb-warp/build_bank2.sh` first). Bank 1 and bank 2 must come from the same build: `b2_expect_sum` and `B2_ID` (`0x42474E31` here) pair them.

To install, copy `BLACKBOX.BIN` and `BBEXTRA.BIN` from `3.1.9-folders-repitch/` to the card root and run BACK+INFO. On the next boot the card loader writes bank 2 (once, then soft-resets). The Tools CPU meter shows `!` until bank 2 is paired.

`test_bank2.py` runs the packed payload the way bank 1 drives it, with a host read hook. It checks that the raw launch is sample-exact, that the handoff has no level dip or step, the per-block cost, and re-initialisation over a clobbered voice slot.

## Memory

- **Code:** bank 2 XIP at `0x08100000`. The header `{BB2!, id, len, sum}` is stamped by `tools/bank2/pack_bbextra.py`, which now also packs the `.data` load image after `.text`.
- **`.data`/`.bss` (~2 KB):** in the SRAM4 voice slot past its 64-byte header (`0x38007040`). They can't go in SRAM1 because the 3.1.Z slice tables use SRAM1/2. Another Warp mode (Smooth/Capi/Vari) overwrites them, so `bg_mix_fw` calls `bg_init` again whenever it reclaims the slot. That costs ~0.5M instructions plus priming.
- **Heap:** newlib `malloc`/`free` on a 2 MB SDRAM arena taken once per boot from the stock bump allocator (`0x0804436C`). The record lives at `0x38005200` (`BGAR`, base, len), and `cpu_boot` clears it on every boot.
- **No EH runtime, stdio or static constructors in the leaf.** `build_bank2.sh` fails the build if any of them get linked.

## Bank-1 fixes that also apply to RB

- `hook_rate` now passes the tempo ratio for mode 6. Before this fix, mode 6 always got `CLIP_SCALE = 1.0`.
- `bg_mix_fw` takes the voice slot over from a foreign magic, as Smooth/Capi/Vari do. `rb_mix_fw` still refuses to, so with `WARP_LEAF=rb` mode 6 only runs if nothing else has claimed the slot since boot (`state_init` seeds Smooth).
