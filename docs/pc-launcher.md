# Program Change launcher (3.1.U)

Status: built and bench-tested on the lineup image (3.1.U and 3.1.UD), **not yet tried on the unit**. Code: `firmware/patches/3.1.9-pc-launcher` (`pcl.c`, `pcl_b2.c`, `pcl.h`, `sites.py`), chained into `../3.1.9-mix-bungee/build_patch.py --lineup`.

## How to use it

- **Open:** press **PSET** to open the Presets screen as usual, then **PSET again**. Four big buttons replace the preset list, 2x2 in the same layout as the four encoders.
- **Close:** PSET once more brings the list back. Leaving the screen closes the layer too, and it opens again the next time you come back to Presets.
- **The buttons:** each shows its program and its MIDI channel, e.g. `[Pgm 12]  Ch 3`. The brackets mark what the encoders edit.
- **Encoders:** each encoder edits the button in its corner:
  - top-left encoder: the top-left button;
  - bottom-left encoder: the bottom-left button;
  - top-right encoder: the top-right button;
  - bottom-right encoder: the bottom-right button.

  By default they turn the **program** (1–128), one step per detent.
- **INFO** switches the encoders to the **channel** (1–16), `Pgm 12  [Ch 3]`; INFO again switches back.
- **Tap a button** to send its program change. It goes to DIN and USB as stock MIDI out does: USB when Tools → USB Dev Out is on, and the 3.1.UD USB device. The button lights for a moment.
- **Saved:** the four are global (`settings.tml`: `pcl1`–`pcl4`), the same on every preset. They are written to the card once, when the layer closes, if anything changed.
- **Defaults:** button n has program n, channel 1.

The layer is in bank 2: without a matching `BBWARP.BIN`, PSET and the Presets screen are stock's.

## How it is built

| Site | Kind | Stock | Does |
|---|---|---|---|
| `0x080A32EA` | bl | `bl App_SetScreen` (PSET, button 6 in `App_HandleInput`) | `pcl_pset_key`: on the Presets screen (app+0x8CA4 = 7), with bank 2 paired, toggles the layer; otherwise stock. |
| `0x080F1498` | vtable | Presets +0x00 tick `0x080BE595` | `pcl_tick`: stock's tick, then bank 2's (`pcl_tick_b2`): build, show / hide, INFO, labels, the press light, the GUI passes. |
| `0x080F14C8` | vtable | Presets +0x30 onChildEvent `0x080BD929` | `pcl_child`: a press on one of ours (ids `0x1D4`–`0x1D7`) fills the mailbox; everything else is stock's. |
| `0x080F14CC` | vtable | Presets +0x34 (the Mix line's `pset_on_event`) | `pcl_event`: encoders (event `0x32`) while the layer is up; on leaving (`0xD3`) closes it and saves; everything else goes on to `pset_on_event`. |
| `0x0804CED8` | bl | `bl FUN_08044AD8` (`Pcm_PollMidi`'s MIDI out pump, audio task) | `pcl_pump`: sends a pending program change `{0xC0 | ch, pgm}` with `FUN_08044A0E` (DIN `0x2401504B`, USB Dev Out `0x2401504D`), then stock's pump. |
| `mx_register` / `mx_global_tpl` (mixer.c) | call | | `pcl_register` (params `0x1E8`–`0x1EB`, the state, the buttons' SDRAM) and `pcl_global_tpl` (defaults). |

**Why the audio task.** `Pcm_PollMidi` runs in the audio task (`audioTask_loop` `0x08041470`), and `FUN_08044A0E` has no lock. So a press only fills a mailbox (backup SRAM: channel, program, then `seq`), and `pcl_pump` sends it from the audio task, once per block.

**INFO** never reaches the Presets screen (`App_HandleInput` has no INFO case for screen 7). The tick edge-detects INFO's debounced level byte `0x24009BDE` (0 = held), as the Song DJ row reads it.

**Stock's widgets** (the list, its slider, Load / Save / File / ^) are hidden under the layer with their hidden flags saved, and come back as they were. If stock shows one again meanwhile (the ^ follows the browse path), the tick hides it again and restores it later as shown.

**Memory:**
- `PclState`: backup SRAM `0x38800624`–`0x38800638`.
- The four buttons and the saved stock flags (`PclBlk`): SDRAM from the boot allocator, about 1 KB.
- Bank 1: about 630 bytes. The screen is in bank 2 (`RbExt.pcl_ui`, plain C: `b2_plain_check`).

## Bench

`PC launcher:` cases in `tools/bench/run_tests.py`, with bank 2 paired:
- **sites:** the hooks, the vtable slots, the registry, the global template;
- **the layer:** on a fake Presets screen with stock buttons:
  - PSET again toggles, PSET elsewhere is stock's, and so is PSET without bank 2;
  - the 2x2 rects, ours as the oldest children, stock's hidden;
  - labels; encoders per corner, clamped; INFO toggles once per press;
  - a press fills the mailbox and lights; release swallowed; stock's presses go to stock;
  - the pump sends `C3 03` (two bytes) with the DIN / USB flags, once;
  - leaving restores stock's widgets and saves the settings once; no save without a change.

## To try on the unit

1. **Open and close:** PSET, then PSET again: four buttons. PSET again: the list.
2. **Encoders and INFO:** the encoders change the programs per corner. INFO switches to channels, INFO again back to programs.
3. **Sending:** with a MIDI monitor (3.1.UD over USB, or DIN), a tap sends the program change on its channel.
4. **Saved:** power-cycle; the values are kept.
