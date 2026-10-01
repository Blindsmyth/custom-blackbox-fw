# Blackbox 3.1.9 reverse-engineering notes

Analysis of [`firmware/bins/3.1.9/BLACKBOX.bin`](../firmware/bins/3.1.9/BLACKBOX.bin). Ghidra is not installed on this machine, so this map was built with Capstone (Thumb, little-endian) in `.venv`. Load the same file in Ghidra later with the base below if you want a GUI.

Stock `firmware/bins/3.1.9/BLACKBOX.bin` was not modified.

## Load address

The file is the application, linked at **`0x08040000`**, not `0x08000000`.

| Word | Value | Meaning |
| --- | --- | --- |
| File + 0x00 | `0x20020000` | Initial stack, top of DTCM |
| File + 0x04 | `0x080403C1` | Reset (Thumb). File offset `0x3C0` |
| Reset code | `ldr r0, [pc]; mov sp, r0; bl 0x08077858` | Sets SP, then branches to init |
| Next | `movw r0, #0xED88; movt r0, #0xE000` | CPACR (`0xE000ED88`): enables the FPU |

`0x08000000`–`0x0803FFFF` is not in this file. That 256 KB is the installer already in internal flash (the “Blackbox Installer / Looking for File / Erasing” screen). Holding BACK+INFO runs that installer, which copies this image to `0x08040000`.

String pointers in the file are `0x08040000 + file_offset`. Example: `Presets` is file `0x8EBC4`, pointer `0x080CEBC4`.

Reset (`0x080403C0`) does not take arguments. An earlier look at file offset `0x403C0` was a normal function in the middle of the image, not the entry point.

## 3.1.2 vs 3.1.9

These are two full links, not a delta patch.

| | 3.1.2 | 3.1.9 |
| --- | --- | --- |
| Size | 695,920 | 728,696 |
| Initial SP | `0x240609B0` (AXI SRAM) | `0x20020000` (DTCM) |
| Reset | `0x08040515` | `0x080403C1` |
| Bytes that differ inside the overlap | essentially the whole file | |

3.1.9 has 981 strings that 3.1.2 does not. The ones that match the changelog are the DJ FX ids: `fxrepeater`, `fxgater`, `fxecho`, `fxbitcrusher`, `fxflanger`, `fxfilter`, `cutoffFx`, `fxalgolivea`, `fxalgoliveb`. Treat function addresses as 3.1.9-only.

## Startup and logging

`0x08043DE4` is the log/printf used by almost every `PresetMgr::` / `SessionMgr::` string. `0x080C3B98` draws a C string on the panel (used for the main-menu labels and the version).

## Version string (the flash test)

One ASCII string `3.1.9` at file offset `0x8F290` (VA `0x080CF290`). Four literal-pool uses, including the main menu at `0x080AEAB2`, which draws it next to the mode names Pads, Keys, Presets, Tools. The test patch changes that single byte so the menu shows `3.1.H`. See [`firmware/patches/3.1.9-version-H/`](../firmware/patches/3.1.9-version-H/).

## Preset browser

`0x240021A4` is the SD drive object (mounted flag at byte `0x15`), not a path string. Preset paths are built by `0x080915A0`, which joins the literal `Presets` and one component through `0x080431A4`. `0x08091860` appends `preset.xml`.

The on-screen list is `0x08091BB8`. It lists every child directory of `\Presets`. Names from the directory pass of `0x08042F64` are stored with a leading `\`, and the builder strips that. `preset.xml` is not consulted there. Choosing a row (`0x08091988`) opens `preset.xml`; if that fails, stock firmware loads a different row that does open.

`ListDirs` at `0x08042CD0` has one caller, `0x080929B4` inside `0x0809283C`, which logs `Deleting Dir:` / `Deleting:`. That is clean/delete. Do not patch it to show folders.

The folder image is [`firmware/patches/3.1.9-preset-folders/`](../firmware/patches/3.1.9-preset-folders/). Detail is in [`preset-folders.md`](preset-folders.md).

## Source paths baked into 3.1.9

All under `C:\Users\kf6gp\Projects\Code\matrixsw\BoomboxFramework\Src\`:

- `AnalogInput.cpp`
- `AudioDriverBoombox.cpp`
- `I2CPortBoombox.cpp`
- `MidiPort.cpp`
- `SDMgr.cpp` (also `Unable to FATFS_LinkDriver`)
- `usbh_conf.c`
