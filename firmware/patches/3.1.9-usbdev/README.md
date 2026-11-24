# 3.1.UD: USB MIDI device on the power jack

A variant of the lineup image (3.1.U plus this), for trying Seq MIDI out from a computer with nothing but a USB cable. Built with `../3.1.9-mix-bungee/build_patch.py --lineup --usbdev`; the images go to `../3.1.9-mix-bungee/lineup/usbdev/`. The menu reads `3.1.UD`.

Ported from blackbox-mod's `src/usbdev.c` (© 2026 Justin Joe, MIT, [LICENSE-blackbox-mod](LICENSE-blackbox-mod)). It's the MIDI part only:
- **Left out:** its USB audio stream and the debug peek / poke requests.
- **Changed:** the receive buffer is read by the poll, as in blackbox-mod's builds before its 3.1.Q, so no interrupt vector is patched.

## Use

1. Connect the Blackbox's **power jack** to the computer with a data-capable USB cable. The jack powers the unit as before.
2. About 2 s after boot the computer sees a class-compliant MIDI device, **Blackbox** (USB id `1209:0002`), with one port each way.
3. Turn on **Tools → USB Dev Out**. That setting fills the "to USB" queue this port sends from, so without it nothing goes out.
4. On a sequence: MIDI Out on a channel, Port **Both** or **USB**.

What goes out is everything the Blackbox would send to its USB-A host jack, but complete: CCs, program change and bend too. The port keeps its own place in that queue (the last 256 messages), so a burst over 64 events a block (the MPE setup for 15 channels is 102), or a block the computer hasn't collected yet, goes out over the next blocks instead of being dropped (blackbox-mod's sent at most 64 a block and skipped a busy block). Stock's own packer for the USB-A jack keeps only notes and clock. That makes it the way to see the MPE setup (MCM, bend range) and the layers' Bank / Sub / Pgm on a computer.

MIDI from the computer goes to stock's USB MIDI input, as if it came from the USB-A jack: MIDI In and Clock settings for USB apply.

## How

| Site | Stock | Does |
|---|---|---|
| `0x080414E4` (diag's `cpu_wait`, already the lineup's) | audio task's wait | `usbdev_poll()` after each wait (`-DWITH_USBDEV` in `tools/diag/diag.c`) |
| `0x08044B08` | `bl FUN_08044896` | `usbdev_midi_out`: the "to USB" queue to the computer first, without consuming it; then stock's packer, as before |

The bring-up waits until stock's USB host controller has its clock and the 48 MHz source, then 400 audio blocks (~2 s). Then it clocks OTG_HS (`0x40040000`) and puts PB14 / PB15 on AF12 (its built-in full-speed transceiver). Last, it resets the core, forces device mode and connects. Stock never touches OTG_HS or those pins.

State: backup SRAM `0x38800650` (32 bytes) and `0x38800908` (the 32-event queue from the computer), as in blackbox-mod. Nothing else on this line uses backup SRAM; `usbdev_poll` enables its clock (`BKPRAMEN`, `DBP`).

## Caution

blackbox-mod shipped this in JOEBOX 1, the release it put **on hold** (2026-10-14) because some units got stuck restarting after installing it. Its README doesn't name the cause, and the same code ran on its author's unit through several test builds (3.1.K–3.1.R).
- **Before installing:** keep a stock 3.1.9 `BLACKBOX.BIN` and this line's 3.1.U on the card under other names.
- **If it doesn't come up:** reinstall one of those with BACK + INFO.

## Bench

`USB device:` cases in `tools/bench/run_tests.py`. They run against a model of the OTG core taken from blackbox-mod's `test_usbdev.py`:
- the bring-up order, pins and clocks;
- no interrupt enabled;
- enumeration with the 101-byte MIDI configuration;
- MIDI from the computer into stock's queue;
- the to-USB queue to the computer, with CCs, program change, notes and bend as USB-MIDI events, while stock's packer still gets its own.

The model is a reading of the core, not the core. That the power jack reaches PB14 / PB15, the real controller and a real host are for the unit.

Flash: about 2.2 KB. Bank 1 has 1.4 KB left in this variant.
