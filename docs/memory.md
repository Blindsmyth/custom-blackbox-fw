# Flash and RAM budget (3.1.9)

STM32H743XI. Code execute-in-place from internal flash. Startup copies `.data` and zeros `.bss` only; it does **not** copy Thumb into RAM. Measured against stock `firmware/bins/3.1.9/BLACKBOX.bin` (728,696 bytes) and the current patches.

## Flash

2 MB at `0x08000000`–`0x081FFFFF`, two 1 MB banks. The SD installer writes `BLACKBOX.BIN` at `0x08040000`. Never overwrite `0x08000000`–`0x0803FFFF`.

| Piece | Range | Size | Status |
| --- | --- | --- | --- |
| Installer | `0x08000000`–`0x0803FFFF` | 256 KB | Occupied. BACK+INFO. Do not touch. |
| Stock 3.1.9 | `0x08040000`–`0x080F1E77` | 712 KB | App + rodata |
| Patch cave | `0x080F1E80`+ | see below | Our hooks, XIP |
| Rest of bank 1 | after cave – `0x080FFFFF` | **~56 KB** | Free, same bank as the app |
| Bank 2 | `0x08100000`–`0x081FFFFF` | **1 MB** | Unused. Not installer-proven |

Current caves (from `0x080F1E80`):

| Image | Cave | End |
| --- | --- | --- |
| Folders 3.1.V | 2.4 KB (`0x96C`) | `0x080F27EC` |
| Warp 3.1.W (Smooth+Snap+Vari+Grain) | 15.9 KB (`0x3F8C`) | `0x080F5E0C` |
| Combined 3.1.Z (folders+Warp+Vari+Grain+slices) | 19.4 KB (`0x4C70`) | `0x080F6AF0` |

| Clip regions 3.1.E (3.1.Z + Start/End/Loop) | 27.9 KB (`0x6F68`) | `0x080F8DE8` |
### The cave size check is not the chip

`build_patch.py` aborts if the cave is unexpectedly large (currently `0xE000`, past bank-1 end is also rejected). That is a tripwire so a bad link does not silently grow the image. Hardware still has ~56 KB in bank 1 after the cave start. Raise the script limit when a real feature needs more. Do not treat the tripwire as “flash is full.”

### What 1 MB unused flash means

Bank 2 is a megabyte of unused XIP space. Thumb-2 is dense: that is a lot of screens, hooks, and engines, not a few more `bl` sites. Bank 1 alone (~56 KB after the cave start) is already enough for many more features of the kind we have added.

Limits that bite first are not flash:

- FatFs is single-task (`pcmStreamer` only). UI hooks may only change RAM and post commands.
- Patch code must not write the card.
- SRAM4 is 64 KB and already holds folder state.
- Stock UI, queues, and the SD installer constrain what we can bolt on.

Bank 2 is a different flash bank. Stay in bank 1 (`0x080F1E80`–`0x080FFFFF`) until we prove the installer writes a `BLACKBOX.BIN` that crosses `0x08100000`.

## RAM

| Region | Size | Stock | Patches | Use |
| --- | --- | --- | --- | --- |
| **SRAM4** `0x38000000` | 64 KB | unused | folders 18.3 KB (`S_SIZE` `0x4930`); Warp flags at `0x38005000` (to `0x38005113`); Clip region, hot cues, grid and Clip editor state at `0x38004930`–`0x38005000` (`0x688` used incl. three row buttons, 3.1.E; magic `DEC4`); slice pool header at `0x38006000`; Smooth/Capi/Vari voice at `0x38007000` (~30 KB, one owner; Capi 28.4 KB); Vari epoch map at `0x3800E800` (~6 KB) | **Patch state.** Magic word; re-init on boot. |
| **SRAM1–3** `0x30000000` | 288 KB | clocked, no static use | 3.1.Y: SRAM1+2 grown slice tables (8 KB each); SRAM3 XML `ext` scratch | First use of this region. |
| **Backup** `0x38800000` | 4 KB | clocks never enabled | none | Skip. Needs `BKPRAMEN` + `DBP`. |
| **ITCM** `0x00000000` | 64 KB | no `.text` copy | none | Only if we start executing from RAM. |
| **DTCM** `0x20000000` | 128 KB | MSP 2 KB + 126 KB bump allocator | none | **Do not use.** 3.1.M collided here. |
| **AXI SRAM** `0x24000000` | 512 KB | `.data` 248 B, `.bss` ~188 KB, heap/stacks ~195 KB, DMA 128 KB | none | **Full.** |
| **SDRAM** `0xC0000000` | 64 MB | samples | none | Not for patch globals. |

## Rules of thumb

1. New code goes at `0x080F1E80` and grows upward in bank 1.
2. New tables and caches go in SRAM4 after the existing maps (folders below `0x38005000`, Warp at `0x38005000`). Do not reuse DTCM, AXI, SDRAM, or backup SRAM.
3. Raise the 8 KB script cap when the cave needs it. Crossing into bank 2 is a separate, proven-installer decision.
4. Flash room is not the same as feature room. I/O, RAM, and stock call sites still bound what we ship.
**SRAM4 on the Bungee / Mix line** (`worktrees/warp-bungee`), for merging: folders `0x38000000`–`0x38004930`; Warp flags `0x38005000`; ADSR `0x38005110`; folder tick `0x38005180`; Mix `0x38005200`; EQ row buttons `0x38005400`–`0x38005680`; Seq Rec label `0x38005700`; Looper `0x38005800`–below `0x38006400`; RbVoice and diag `0x38007000`–`0x38007148`; Bungee grain input `0x38008000`–`0x38010000`. 3.1.E's deck block (`0x38004930`–`0x38005000`) is free there. 3.1.T adds Song cues at `0x38006400`–`0x38006C2C` (`SgState`, deck.c), between the Looper and RbVoice. Still overlapping on a merge: 3.1.Y's slice pool header (`0x38006000`) with the Looper, and Smooth/Capi/Vari (`0x38007000`+) with RbVoice / Bungee.

