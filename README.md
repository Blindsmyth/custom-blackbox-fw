# Blackbox preset folders

A patch for 1010music **blackbox** firmware **3.1.9** that adds nested folders to the preset browser. The menu version reads **3.1.U**.

Stock 3.1.9 lists one level of `\Presets`. A folder without its own `preset.xml` cannot be opened; Load falls through to some other preset. This patch treats those folders as groups:

- A group is listed with a leading `/`. Load on it stays on the preset screen and shows its contents.
- Inside a group, `..` and the BACK key go up one level. Neither goes above `\Presets`.
- Groups can be nested (`\Presets\Kits\Drums\808\preset.xml`).
- Save As, New, Rename, Delete and Clean act on the folder you are browsing. Plain Save and Pack still write next to the loaded preset.

Details, limits, and how to flash: [firmware/patches/3.1.9-preset-folders/README.md](firmware/patches/3.1.9-preset-folders/README.md). Design: [docs/preset-folders.md](docs/preset-folders.md).

**Not affiliated with, endorsed by, or supported by 1010music.** This repository does not contain 1010music's firmware. Download 3.1.9 from [1010music.com/downloads](https://1010music.com/downloads), put `BLACKBOX.bin` in `firmware/bins/3.1.9/`, and check the sha256 in `firmware/manifest.json`. Built images stay local and are gitignored. Running modified firmware may affect your warranty. Flash stock before asking 1010music for support.

## Build

```
.venv/bin/python firmware/patches/3.1.9-preset-folders/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-preset-folders/BLACKBOX.BIN
```

Needs a Python venv with `unicorn` and `capstone`, plus `clang`, `dosfstools`, and `mtools`. How the patch was made: [docs/workflow.md](docs/workflow.md).

## Layout

```
firmware/patches/3.1.9-preset-folders/   cave.S, build script, patch notes
firmware/bins/3.1.9/                     stock BLACKBOX.bin (local only)
firmware/manifest.json                   version hashes
docs/                                    firmware map, design, workflow
tools/                                   Ghidra import and Unicorn bench
```

Do not commit 1010music firmware, patched `BLACKBOX.BIN` images, manuals, artwork, or a full disassembly of the stock image.

AI (Cursor) assisted with the reverse engineering, tools, patches and docs.
