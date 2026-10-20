# 3.1.9 Rubber Band Warp (bank 2)

Mixxx-style **Rubber Band R2** (“faster”) as Warp enum **`RB`** (value 6), with the DSP leaf in **flash bank 2**.

**License:** Rubber Band is GPL-2.0-or-later. Any image that links it is GPL.

## Status

| Piece | State |
| --- | --- |
| Optimized M7 probe (`OPT=cmsis`) | Done — ~55–60% of one 256-frame budget ([RESULTS.md](../../tools/lab/rb/m7/RESULTS.md)) |
| `BBEXTRA.BIN` pack | Done — `./build_bank2.sh` → `out/BBEXTRA.BIN` (~627 KB) |
| Warp UI label `RB` | Wired in folders-repitch `repitch.S` |
| Bank-1 `rb_mix_fw` + `b2_live` | Wired; calls bank-2 via `bank2_syms.h` |
| Card loader (`b2load.c`) | **Enabled** (`B2LOAD_WRITE=1`) — programs bank 2 from `BBEXTRA.BIN` on first unpaired boot |
| Tools CPU meter | Wired (`cpu_wait` / `cpu_tools`); `!` while bank 2 unpaired |
| Hardware soak | In progress |

## Build

```sh
# 1) Bank 2 leaf + BBEXTRA.BIN (needs Arm GNU Toolchain + CMSIS tree under tools/lab/rb/m7/cmsis)
firmware/patches/3.1.9-rb-warp/build_bank2.sh

# 2) Refresh Thumb addresses in rb/bank2_syms.h from out/bank2.elf if they moved
arm-none-eabi-nm out/bank2.elf | grep 'T rb_'

# 3) Bank 1 cave (folders + Warp + RB hooks)
python3 firmware/patches/3.1.9-folders-repitch/build_patch.py
```

Install: put `BLACKBOX.BIN` and `BBEXTRA.BIN` on the card root (folders-repitch build copies both). BACK+INFO installs bank 1; on the next boot the card loader writes bank 2 when unpaired, then soft-resets once. See [docs/re-notes.md](../../../docs/re-notes.md).

## Runtime rules

- Clip only, one owner (shares Smooth/Capi/Vari slot at `0x38007000`).
- ~8 MB SDRAM arena via stock bump `0x0804436C` on first `rb_init`.
- Unpaired / missing bank 2 → fall through to stock Stretch (no fault).
- Never erase bank 2 sector 7 (`0x081E0000`, factory calibration).
- Loader only erases sectors 0–5 after a blank/claim check; foreign bank 2 is left alone (`B2_FOREIGN`).
