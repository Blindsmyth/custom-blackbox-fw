# Blackbox hardware

Original 1010music **blackbox** (not blackbox 2). Pin-level notes come from the community board crate [mstaack/blackbox-rs](https://github.com/mstaack/blackbox-rs) (`src/lib.rs`, README). That crate brings the board up; it is not the 1010music firmware. Nothing below was measured on this unit.

Teardown reference: Olivier Ozoux, “Inside the 1010 Music Blackbox” (Matrixsynth, July 2020). The STM32H743 identification matches the Reddit teardown note and the crate.

## MCU and memory

| Item | Value | Verified here |
| --- | --- | --- |
| MCU | STM32H743XI, Cortex-M7, rev.Y | Not on this bench. Vector table in `firmware/bins/3.1.9/BLACKBOX.bin` matches this part: initial SP `0x20020000` (top of 128 KB DTCM), code linked at `0x08000000`. |
| Clock | HSE 6.144 MHz, PLL1 sysclk 399.36 MHz | Not measured. Official comparison chart lists “400 MHz ARM”. |
| Internal flash | 2 MB at `0x08000000` | The 728,696-byte app is linked at `0x08040000`. The first 256 KB is the installer already on the chip; it is not inside `BLACKBOX.BIN`. |
| DTCM | 128 KB at `0x20000000` | Inferred from the initial stack pointer. |
| SDRAM | 2× IS42S16160J, 64 MB at `0xC0000000`, FMC bank 1 | Not measured. |
| Cache | rev.Y erratum ES0392: D-cache left off; SDRAM and D2 SRAM marked non-cacheable | From the crate, not from this firmware. |

## Display and touch

- Panel: 320×240 RGB565, LTDC, 25 data/control pins on AF14. Pixel clock from PLL3 at 6.4 MHz.
- Panel power: **PK7**. Backlight via TIM8 / **PJ6**, clamped to 35% in the crate because of the boost regulator.
- Touch: GT9147 on shared I2C1. INT is **PG12**. Address latches at power-on: `0x5D` when INT is low at reset, `0x14` if the strap is wrong. A real power cycle is required to re-latch.

## Audio

- Codec: CS42528 at I2C `0x4C`. Reset GPIO **PG13**.
- SAI1 I2S master TX: MCLK **PE2**, FS **PE4**, SCK **PE5**, SD **PB2** (AF6). DMA1 channel 0.
- Sample clock: PLL2 12.288 MHz = 256 × 48 kHz.
- Official I/O (product page, not the crate): 1 stereo in, 3 stereo outs, headphone out, 24-bit converters. The crate only documents the headphone SAI path.

## Controls

Buttons are active-low with board pull-ups. PA0, PA1, PC2, and PC3 need the STM32H7 SYSCFG dual-pad fix (the crate calls `dual_pad_fix`).

| Button | Pin |
| --- | --- |
| PADS | PI8 |
| KEYS | PD4 |
| SEQS | PD7 |
| SONG | PI12 |
| FX | PI14 |
| MIX | PG3 |
| PSET | PH7 |
| TOOLS | PC13 |
| REC | PB13 |
| STOP | PJ1 |
| PLAY | PH4 |
| BACK | PC2 |
| INFO | PC3 |

LEDs are active-high, board order: PG9, PJ8, PB10, PB8, PB9, PK2, PA5, PJ5, PJ4, PB11, PA4.

Encoders are four Alps endless pots, each with two analog wipers 90° apart, read on ADC1 (16-bit). Angle is `atan2` of the pair.

| Knob | Wiper A | Wiper B |
| --- | --- | --- |
| Top-left | PA0 | PA1 |
| Top-right | PA6 | PC4 |
| Bottom-left | PB1 | PA7 |
| Bottom-right | PC5 | PB0 |

PA0 and PA1 are both ADC wipers and are called out in the dual-pad fix. Treat that overlap as unverified against the shipping unit.

## Storage, buses, firmware update

- I2C1: **PB6** SCL, **PB7** SDA, 400 kHz. Touch and codec share it.
- microSD: FATFS / exFAT in the stock firmware (`SDMgr.cpp`, string `Unable to FATFS_LinkDriver`). Card holds presets, WAVs, and the update file. Pin mux for SDMMC is not in the crate (it does not mount a card).
- Firmware lives in internal flash. Update: put `BLACKBOX.BIN` on the card, power on holding **BACK + INFO**. The installer prints “Blackbox Installer”, “Looking for File”, then “Erasing”. Presets on the card are left alone.
- USB: the firmware links `usbh_conf.c` (USB host). The product uses USB MIDI device-in. Host vs device role on this connector was not checked here.
- SWD: the crate uses probe-rs with chip `STM32H743XI`. Do not use a probe until an SD-card string patch has booted.

## What this unit’s firmware confirms

Strings in 3.1.9 name the application `BoomboxFramework` (`C:\Users\kf6gp\Projects\Code\matrixsw\BoomboxFramework\Src\`). Modules referenced by path: `SDMgr.cpp`, `AudioDriverBoombox.cpp`, `AnalogInput.cpp`, `I2CPortBoombox.cpp`, `MidiPort.cpp`, `usbh_conf.c`. See `docs/re-notes.md` for addresses.
