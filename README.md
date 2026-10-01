# Blackbox Firmware Archive and Mods

This repo holds an archive of 1010music **blackbox** firmware binaries and release notes. It also holds reverse-engineering work on 3.1.9 and a patched build, **3.1.N**, that adds group folders to the preset browser ([firmware/patches/3.1.9-preset-folders/](firmware/patches/3.1.9-preset-folders/)). It's for research on hardware you own. The firmware is 1010music's, so keep this repo private.

## Layout

```
firmware/
  zips/           original downloaded ZIP packages
  bins/<ver>/     extracted BLACKBOX.BIN (or .bin) per version
  gamechanger/    separate Gamechanger for Blackbox build
  patches/        patched images built from stock 3.1.9 (sources + build scripts)
  manifest.json   versions, URLs, sizes, sha256 hashes
docs/
  map-319.md      firmware map: memory, RTOS tasks, FatFs config, PresetMgr
  preset-folders.md  design and bench results of the folder patch
  versions.md     comparison across versions
  symbols/        function names per version (applied to Ghidra)
  decomp/3.1.9/   curated decompiled functions
tools/
  ghidra_import.sh, ghidra/   headless Ghidra import, naming and export
  decomp.py, carry_names.py   query exports, carry names across versions
  bench/          Unicorn test bench: runs firmware code against a FAT32 card image
CHANGELOG.md      merged history + per-release notes
```

## Modding workflow

1. **Map before patching.** Import the image into Ghidra headless (`tools/ghidra_import.sh <ver>`) at base `0x08040000` with the STM32H7 memory map. Name functions in `docs/symbols/<ver>.csv` and record the facts with evidence in `docs/map-319.md`: library config, tasks, RAM use, and object layouts.
2. **Design from the map.** File I/O only runs from the task that owns FatFs (pcmStreamer). UI-task hooks only touch RAM and post commands. Patch code never writes to the card. Patch state lives in RAM that nothing in stock firmware uses, and that includes computed addresses, not just literals.
3. **Build** with `firmware/patches/<name>/build_patch.py`. It assembles `cave.S` with clang, links it at `0x080F1E80` (past the end of the stock image), checks the stock bytes at every hook site, patches branches, and sets the version letter.
4. **Bench before the device.** Run `tools/bench/run_tests.py --image <bin>` on both the stock and the patched image. It runs the real firmware functions in Unicorn against a write-protected FAT32 image. It checks behaviour, that no write was attempted, that the card image is unchanged, and that `fsck.fat -n` is clean.
5. **Install** only through the SD installer: `BLACKBOX.BIN` in the card root, then power on holding BACK+INFO. Keep a stock copy on the card under another name. Never use SWD and never touch the bootloader.
6. **When the device disagrees with the bench,** find what the bench skips, add a test that reproduces the failure, and only then fix it.

AI (Cursor) assisted with the reverse engineering, tools, patches and docs.

## Sources

- Forum index (requires login): https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads
- Official current firmware page: https://1010music.com/downloads (3.1.9)
- History 2019 / 2020 threads linked in `manifest.json`

## Collected versions (22 packages)

- **gamechanger-0.1.2** — `gamechanger012.zip` → `BLACKBOX.BIN` (389884 bytes)
- **1.0.2** — `Blackbox102.zip` → `BLACKBOX.BIN` (468776 bytes)
- **1.0.6** — `Blackbox106.zip` → `BLACKBOX.BIN` (465796 bytes)
- **1.1.1-beta** — `Blackbox111.zip` → `BLACKBOX.BIN` (483408 bytes)
- **1.2.2-beta** — `blackbox122.zip` → `BLACKBOX.BIN` (581988 bytes)
- **1.3.5-beta** — `blackbox135.zip` → `BLACKBOX.BIN` (591792 bytes)
- **1.3.6** — `blackbox136.zip` → `BLACKBOX.BIN` (591552 bytes)
- **1.4.0** — `blackbox140.zip` → `BLACKBOX.BIN` (602884 bytes)
- **1.4.3** — `blackbox143.zip` → `BLACKBOX.BIN` (604340 bytes)
- **1.5.1** — `blackbox151.zip` → `BLACKBOX.BIN` (558940 bytes)
- **1.6.5** — `blackbox165.zip` → `BLACKBOX.BIN` (581780 bytes)
- **1.7.4** — `blackbox174.zip` → `BLACKBOX.BIN` (611068 bytes)
- **1.7.F** — `blackbox17f.zip` → `BLACKBOX.BIN` (633148 bytes)
- **2.0.E** — `BLACKBOX20E.zip` → `BLACKBOX.bin` (666348 bytes)
- **2.1.5** — `BLACKBOX215.zip` → `BLACKBOX.bin` (672164 bytes)
- **2.1.5L** — `BLACKBOX215L.zip` → `BLACKBOX.bin` (671740 bytes)
- **2.9.1-beta** — `BLACKBOX291.zip` → `BLACKBOX.bin` (684428 bytes)
- **3.0.1** — `BLACKBOX301.zip` → `BLACKBOX.bin` (689404 bytes)
- **3.0.9** — `BLACKBOX309.zip` → `BLACKBOX.bin` (691452 bytes)
- **3.0.15-beta** — `BLACKBOX3015.zip` → `BLACKBOX.bin` (695968 bytes)
- **3.1.2** — `BLACKBOX312.zip` → `BLACKBOX.bin` (695920 bytes)
- **3.1.9** — `blackbox-3.1.9.zip` → `BLACKBOX.bin` (728696 bytes)

## Missing / dead links

- **1.9-beta** — ZIP download removed from thread (only upgrade guide PDF remains); forum points users to 2.1.5 thread
- **1.7.0** — Linked ZIP returns 404; 1.7.4 and 1.7.F from same thread are archived
- **1.4.1** — Linked ZIP returns 404; 1.4.0 and 1.4.3 are archived

## How this was built

1. Crawled each firmware release thread on the retired 1010music forum (authenticated session).
2. Downloaded ZIP packages from `1010music.com/wp-content/uploads/...`.
3. Extracted `BLACKBOX.BIN` / `BLACKBOX.bin` into versioned folders.
4. Hashed each ZIP and BIN into `firmware/manifest.json`.
5. Merged history threads + per-thread notes into `CHANGELOG.md`.

Forum is retired (read-only); grab archives while downloads still resolve.
