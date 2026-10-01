# 3.1.9 version-label test

Same-length edit of stock 3.1.9. One byte: the menu string `3.1.9` (file offset `0x8F294`) is now `3.1.H`. No other byte differs. Image base is `0x08040000`; see `docs/re-notes.md`.

`BLACKBOX.BIN` in this folder is the file the installer looks for. The stock image is still `firmware/bins/3.1.9/BLACKBOX.bin`.

## Flash

1. Copy stock `firmware/bins/3.1.9/BLACKBOX.bin` onto the card under a different name, for example `BLACKBOX-3.1.9-stock.BIN`. The installer ignores that name.
2. Copy this `BLACKBOX.BIN` to the root of the card.
3. Power on holding BACK and INFO. Wait until it finishes and reboots.
4. Open the screen that lists Pads, Keys, Presets, Tools. The version in the corner should read `3.1.H`.
5. Load a preset and confirm audio still plays.

To go back, copy the stock file to `BLACKBOX.BIN` on the card and run BACK+INFO again.
