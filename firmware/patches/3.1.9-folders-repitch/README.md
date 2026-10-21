# 3.1.Z preset folders + Warp + 2048 slices

This image is stock 3.1.9 plus nested preset folders (`3.1.V`), Clip/Slicer Warp (incl. Vari), and a 2048-slice cap. The menu version reads `3.1.Z`. New code is appended at `0x080F1E80`. Stock `firmware/bins/3.1.9/BLACKBOX.bin` is unchanged.

Folder state stays in SRAM4 at `0x38000000`. Warp flags sit at `0x38005000`. The slice pool header is at `0x38006000`. Grown slice tables live in SRAM1–2. Vari epoch map sits at `0x3800E800` (after the shared voice).

## What it does

Everything in [../3.1.9-preset-folders/README.md](../3.1.9-preset-folders/README.md): group folders, `..` / BACK, Save As into the browsed folder.

Everything in [../3.1.9-clip-repitch/README.md](../3.1.9-clip-repitch/README.md): on a Clip or Slicer pad, **Pos** has **Warp:** Stretch, Repitch, Smooth, Snap, Vari, or Grain. `repitch` in `preset.xml` is ignored by stock 3.1.9. Smooth/Snap/Vari share the voice slot at SRAM4 `0x38007000` (past the slice-pool header). Grain stays on stock `Clip_StartGrain` with a 2048-sample window. Use Sync-to-Slice before A/B’ing Snap.

On Clip or Slicer, **Slices** and **Slice** go to 2048. Slice-to-grid, cue import, and Split can fill that. Sync set to Slice can lock a 512-beat stem to 16ths. Pads at or under 128 keep the stock 512-byte table; past that, an 8 KB block is taken from SRAM1+2. Pad apply posts at most 128 engine messages, then copies the full cell list into the clip so Sync-to-Slice still sees every position.

`preset.xml` stays stock-legal: at most 128 `<slice>` tags. Positions past that are in a `ext` attribute on `<slices>`. Stock 3.1.9 loads the first 128 and ignores `ext`. Saving that preset on stock drops `ext`. MIDI notes and the grid keyboard still reach 128 slices.

## Folder fixes (2026-10-21)

Also in 3.1.E, which has its own copy of `cave.S`. Details: [docs/preset-folders.md](../../../docs/preset-folders.md).

- **No dropout when entering a big folder.** Each subfolder row costs one `f_stat`, not two. The Load press checks at most 4 new rows; the rest are checked 2 at a time between sample-streaming steps, and their `/` marks appear when they are done. The last 8 folders keep their marks, so going back into one reads no more than stock does. On the bench, a folder of 88 subfolders took 5,542 sector reads in one go. Now the press takes 193, and the rest is spread over 42 steps of at most 231. Re-entering takes 173 reads.
- **Delete and Clean never trust the marks.** They check the card for `preset.xml` each time, so a lost mark cannot let Delete erase a group folder.
- **Saving in deep folders.** Stock `path_join` cuts paths at 255 characters and overran its stack buffer past ~280: Save As in a deep folder made the folder, then reset the unit or wrote no `preset.xml`. It is now bounded. Save As and New refuse a name whose `preset.xml` path would not fit, or that names a group folder, and write nothing. A failed save, including a failed `doc_save` on plain Save, shows the preset screen's error message. The Load the UI sends after Save As then keeps the current session; before, it loaded the first row of the list.
- **Save As on a group row** pre-fills the keyboard with the loaded preset's name instead of `/Group`. A typed name that matches a group folder gets numbered (`Sub 1`).
- Entering a folder stops at 241 characters of path, so a preset inside can still be loaded and saved.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under another name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. The Pads / Keys / Presets / Tools screen should show `3.1.Z`.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.

## Rebuild and test

```
.venv/bin/python firmware/patches/3.1.9-folders-repitch/build_patch.py
.venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch/BLACKBOX.BIN
.venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
```

The bench runs the folder cases, the Warp cases, and the slice-cap cases on this image. The exFAT Save As case needs macOS (`hdiutil`, `newfs_exfat`, `fsck_exfat`) and is skipped elsewhere.

The build reads `tools/stretch/snap.c`, `snap.h` and `snap_fw.c`. Clip Deck deleted them in favour of Capi, but `repitch.S` here still calls `snap_init` and `snap_mix`. Until that is resolved, restore them before building: `git show 7227693^:tools/stretch/snap.c > tools/stretch/snap.c`, and the same for the other two.
