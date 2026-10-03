# Reverse-engineering and modding workflow

How the 3.1.9 preset-folder patch (now 3.1.O) was made, and how to make the next one.

## 1. Collect versions

Firmware binaries stay local (gitignored). `firmware/manifest.json` and `CHANGELOG.md` list versions, hashes, and release notes. Download ZIPs yourself into `firmware/zips/`, extract `BLACKBOX.BIN` into `firmware/bins/<ver>/`, and verify sha256. Older, smaller builds are easier to read and help name functions in 3.1.9. The changelog tells you when features appeared. For example, preset folders arrived in 1.5.1, and no version ever supported nested folders. See [versions.md](versions.md).

## 2. Map the firmware before patching

- Ghidra runs headless (`tools/ghidra_import.sh <ver>`) and imports the image at `0x08040000` with the STM32H7 memory map.
- Function names live in `docs/symbols/3.1.9.csv` (about 150 names) and are re-applied with `tools/ghidra_import.sh 3.1.9 post`.
- `tools/carry_names.py` carries names to other versions by matching strings and code shape.
- `tools/decomp.py` queries the exports (`show`, `callers`, `grep`, `strings`).

[map-319.md](map-319.md) records each fact with its evidence and how sure we are:

- **Tasks.** All preset file I/O runs in pcmStreamer. The UI runs in defaultTask.
- **FatFs build options.** There is one shared sector buffer and no locking. That is why 3.1.L's debug-log write from the UI task corrupted the card.
- **RAM layout.** Which regions stock code uses, including memory handed out by its allocators.
- **Preset code.** The PresetMgr object, its command and event queues, and the call chain behind the Load button.

## 3. Design from the map

These rules came out of past failures:

- Only the task that already owns FatFs may touch files. UI hooks only change RAM and queue commands.
- Patch code never writes to the card.
- Patch state goes in RAM that is proven unused. That means checking for computed addresses, not just stored ones (the 3.1.M lesson, see below).
- Measure costs before choosing a design. The Load-time folder check is one `f_stat` of the selected row. The `/` folder marker (3.1.O) adds two `f_stat`s per directory row while the list is built: 4,227 sector reads vs 1,049 stock on a 146-row tree (`tools/bench/time_list.py`).

## 4. Build

`firmware/patches/<name>/build_patch.py`:

- assembles `cave.S` with clang and links it at `0x080F1E80`, past the end of the stock image;
- checks the original bytes at every hook site and refuses to patch if any differ;
- writes the branches into the stock code and sets the version letter;
- confirms nothing else in the stock part changed;
- writes `BLACKBOX.BIN`, `BLACKBOX.sym.json` (cave symbols for the bench) and `cave.dis`.

## 5. Test on the Mac first

`tools/bench/run_tests.py --image <bin>` runs the real firmware code in Unicorn against a FAT32 card image built with `tools/bench/mkcard.py`. Only the lowest-level disk functions and a few RTOS calls are replaced.

- The card is write-protected for read-only tests, so any write attempt fails those. Every test also checks that `fsck.fat -n` is clean. Tests of Save As, Delete, Rename, New and Clean allow writes and check the exact set of files that changed.
- Before each test, the bench fills DTCM through the stock allocator, the way the app's startup does on the device.
- Stock must pass too. That shows the bench behaves like the real device. For example, it reproduces stock's "loads some other preset" bug.
- For realistic runs, mirror the real card's `\Presets` into a bench image. Keep the real preset files and stub the WAVs.

## 6. Install safely

- Only through the SD installer: `BLACKBOX.BIN` in the card root, then power on holding BACK+INFO.
- Keep a stock copy on the card under another name (for example `BLACKBOX-3.1.9-stock.BIN`). To roll back, copy it to `BLACKBOX.BIN` and run the installer again.
- Never use SWD, and never touch the bootloader or `0x08000000`.

## 7. When the device disagrees with the bench

1. Gather facts from the device: the version shown, what loads, and whether it stays responsive.
2. Find what the bench skips.
3. Add a test that reproduces the failure.
4. Only then fix it.

That is how 3.1.M became 3.1.N. On the device, every preset operation failed while the UI stayed responsive. The bench was missing the app's startup allocations. Stock's small-block allocator hands out DTCM from `0x20000000`, which is where the patch kept its state. A new bench test reproduced the overwrite, and moving the state to SRAM4 (`0x38000000`) fixed it.

3.1.N could enter a group folder but Save As wrote the new preset next to the loaded one, then reloaded from the browsed folder and fell back to another row. Delete, Rename, New and Clean had the same wrong-folder problem. 3.1.O wraps those dispatcher calls so they act on the folder you are browsing, and it adds BACK-to-parent and a `/` marker on group rows.

## Setup on a new machine

- Python venv with `unicorn` and `capstone`.
- `dosfstools` and `mtools` for the bench card images.
- Ghidra and openjdk@21 (Homebrew) for the analysis.
- clang for building `cave.S`.
