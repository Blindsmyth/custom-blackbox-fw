# Blackbox 3.1.9 firmware: what we know

The goal is full understanding of the stock firmware. This page is the index by subsystem. Detail lives in [../map-319.md](../map-319.md) (deep dives, per patch), names in [../symbols/3.1.9.csv](../symbols/3.1.9.csv), Ghidra decompiles in `~/GhidraProjects/out/3.1.9/` (query with `tools/decomp.py`). Addresses are 3.1.9 (image at `0x08040000`).

Confidence marks: **S** read from code, **L** one step inferred, **?** open question.

**Rule:** anything learned while reverse engineering goes into these docs in the same commit as the code that relies on it, including open questions.

## Contents

- [Platform, tasks, memory](#platform-tasks-memory)
- [UI: widgets, screens, redraw](#ui-widgets-screens-redraw)
- [Input: buttons, LEDs, encoders, touch](#input-buttons-leds-encoders-touch)
- [Params: registry, bags, persistence](#params-registry-bags-persistence)
- [Sequencer: sequences, player, transport, recording](#sequencer)
- [Clip / Slicer engine](#clip--slicer-engine)
- [FX, EQ](#fx-eq)
- [Presets, files](#presets-files)
- [Tools and the bench](#tools-and-the-bench)
- [Open questions](#open-questions)

## Platform, tasks, memory

- STM32H743, Cortex-M7, double-precision FPU (Unicorn can't execute `.f64` VFP ops; the bench stubs those functions). **S**
- Memory map, patch budget and SRAM4 layout: [../memory.md](../memory.md) (timestretch branch) and `.cursor/rules/memory-layout.mdc`. **S**
- Tasks:
  - **defaultTask** `0x08043DEC`: the UI loop. Input → App vtable+8 → draw `FUN_0808B2D0`. **S**
  - **pcmStreamer**: FatFs and preset load/save; the only task that may touch the card. **S**
  - **audioTask** `0x08041470`: DMA half-buffer loop. Calls the audio callback `FUN_0804CB1C`, which scans buttons, renders and posts to the UI. **S**
- **M7 gotcha:** LDM/STM/LDRD/STRD fault on unaligned addresses (plain LDR doesn't). The bench can't see this; `build_patch.py` checks cave tables. **S**
- HAL tick: `HAL_GetTick` `0x0807794C` reads `uwTick` at `0x24015CE4` (ms). **S**
- Sample clock: SE+0x00 is a u64 count of samples at 48 kHz that runs whether or not the transport is playing (`FUN_080696D8`). **S**

## UI: widgets, screens, redraw

Details: [../map-319.md#mix-and-mute](../map-319.md#mix-and-mute) and [#encoders-lists-seq-eq-and-fx-return](../map-319.md#encoders-lists-seq-eq-and-fx-return-31q-batch-01).

- **Widget base** (`FUN_080AEC08`, 0x34 bytes): rect at +4..+0x10, child lists at +0x18/+0x1C, next sibling +0x20/+0x24, parent +0x2C, hidden flag +0x30, dirty bytes +0x31/+0x32 (full/partial). **S**
- **Vtable slots:** +0 tick(w, flags), +4 draw(w, ctx), +0xC contains, +0x10 touchDown, +0x14 touchMove, +0x18 touchUp, +0x1C acceptsTouch, +0x20 setRect, +0x24 setId (stores +0x14), +0x28 hitTest, +0x2C setHidden, +0x30 onChildEvent (return non-zero to propagate), +0x34 onEvent. **S**
- **Tree helpers:**
  - `FUN_080AEC44` add child (prepends; the draw walks newest first, the hit-test walks oldest first);
  - `FUN_080AEAF8` draw children; `FUN_080AEB44` tick children; `FUN_080AEB5E` hit-test; `ui_post_event_up` `0x080AED14`. **S**
- **Redraw:** the GUI tick gathers flags[0] (full pass, draw ctx `{1,0}`) and flags[1] (partial pass, `{0,1}`). Some labels paint only in the partial pass. A widget's tick must OR its dirty bytes into the flags, or nothing repaints. **S**
- **Canvas:** GUI+0x274, 8-bit palette (`0x24000000`), rotation 1. **The logical y axis runs bottom-up on the panel**, so logical y = 0 is the physical bottom. Fills go through DMA2D (`FUN_08041C68`); set-pixel is `FUN_0808F524`; text width is 12 px per character (`FUN_0808ED28`). **S**
- **Screens:** `App_SetScreen(0x24020088, id, 0, 0)` `0x0809EAEC` posts a show event; the GUI root (`FUN_0808BE4C`, update `0x0808B654`) switches the container (GUI+0x280, active screen at +0x34) with `FUN_080AB4FC`. That sends the old screen 0xD3 and the new one setRect (0,0,320,220) plus 0xD4. **S**

| Screen | Id | GUI offset | Ctor | Vtable |
|---|---|---|---|---|
| Pads | 2 | +0x490 | `0x080B4528` | `0x080F0E50` |
| Pad edit / INFO pages | 6 | +0xF4D0 | `0x080A5B90` | `0x080F0004` |
| Presets | 7 | +0x17D30 | — | — |
| Main menu | 9 | +0x15F10 | `0x080AE8F0` | — |
| Tools (Main/Rec/MIDI/Clock) | 0x25–0x28 | +0x1FA58 | `0x080C5450` | `0x080F18A4` |
| FX Return | 0x23 | +0x36910 | `0x080ABA84` | `0x080F07BC` |
| Seq | 0x2C | +0x39F90 | `0x080B4F34` | `0x080F0E90` |
| Song | 0x2D | +0x59008 | `0x080C2980` | `0x080F1758` |
| Mix / Mute | 0x2E / 0x2F | +0x3E258 | `0x080B5F98` | `0x080F0F10` |
| Keys | 0x2B / 0x31 | +0x57780 / +0x6EBE8 | `0x080AF910` / `0x080AE1A0` | `0x080F0C48` / `0x080F0978` |
| Piano roll / seq editor | ? | +0x45E40 | `0x080C04DC` | `0x080F1658` |
| FX send | 0x30 | +0x6BD40 | `0x080ABF48` | `0x080F0808` |
| EQ | 0x36 | +0x8DE50 | `0x080AA290` | `0x080F06BC` |
| DJ (Live) FX | 0x37 | +0x905A0 | `0x080B20F0` | `0x080F0D90` |
| List screens | ? | +0x732D8 / +0x83F58 | `0x080B8C0C` / `0x080B7B1C` | `0x080F1284` / `0x080F0FEC` |

- **Widget classes:**

| Class | Ctor | Vtable / size | Notes |
|---|---|---|---|
| Frame button | `0x080A3D34` | `0x080EFE48`, 0xA0 bytes | Text `FUN_080A3DEC`, highlight `FUN_080A3E00`, press = child event 1 `{+0xC id}`. **S** |
| Button bar | `0x080C4E70` | `0x080F1864` | Up to 5 buttons at +0x3C + i·0xA0, laid out right to left. **S** |
| Side fader | `0x080C1F3C` | `0x080F16D8` | Set with `FUN_080C20D0`; touchMove posts 0x6E. **S** |
| Value slider | — | `0x080F0C88`, 0x2E0 bytes | Bind `FUN_080A7EE0`, step `FUN_080B0D54`, list accumulator `FUN_080B0B74` (400 units per entry). **S** |
| Scroll list | `0x080B9C84` | `0x080F135C` | Row tap 0x145 → `FUN_080B9C2A`; encoder row `FUN_080B9BA4`, value `FUN_080B9A3C`. **S** |
| Pad cell | `0x080A4A0C` | `0x080EFFC4` | Child events 3/4 = press/release. **S** |
| Label | — | `0x080F17A4` | `ui_draw_cstring` `0x080C3B98` copies the text and marks it dirty. **S** |

## Input: buttons, LEDs, encoders, touch

- **Buttons:** table at `0x24009AB0`, 13 × 0x18. +0xE is the debounced level (0 = pressed); a long press is >600 scan ticks. Order: PADS, KEYS, SEQS, SONG, FX, MIX, PSET, TOOLS, REC (`0xF4`/`0xF5`), STOP (`0xF6`), PLAY (`0xF7`), BACK (7 / long 8), INFO (`0xC`). Scanned by `FUN_080437D4` in the audio callback into a UI ring (+0x604) and an audio ring (+0xC0C). **S**
- **App_HandleInput** `0x080A2E60`: the mode-button jump table is at `0x080A3258`; FX block `0x080A32A4`; REC press `bl FUN_080A16C4` at `0x080A2F3C`. **S**
- **LEDs:** `FUN_080436F8(_, index, on)` writes GPIO BSRR (atomic); table at `0x24009A2C`. REC = 8, PLAY = 10. The audio task rewrites LED 8 only on events, from `FUN_080525D8` (playing && recording). **S**
- **Encoders:** event 0x32 `{+0xC logical index, +0x10 delta}`; logical order is column-major (0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right). Delivered to the active screen's onEvent. **L** (not checked on hardware)
- **Touch** reaches screens through hit-test; pad cells, faders, buttons and list rows handle it themselves. **S**

## Params: registry, bags, persistence

- **Registry:** `Param_RegisterAll` `0x0808C158`. Enum `0x0808C0D8(regs, id, title, labels, count, xml)`; int `0x0808C12C(regs, id, ?, title, min, max, xml)`. Getters: title `0x0808E220`, type `0x0808E24C`, labels `0x0808E278`, count `0x0808E2A4`, min `0x0808E2D0`, max `0x0808E304`, xml name `0x0808E33C`. **S**
- **Bags:** `Param_Get` `0x08093E9C`, `Param_Set` `0x08093ECC` (silently a no-op when the id is missing), `Param_Add` `0x08093EF6`. Templates: `FUN_08093F20` by group (0x47 sequence layer 0, 0x48 layers B–D, 0x36 EQ, 0x26 Delay, 0x4B Reverb); global bag App+0x8C90 with template `FUN_0809481C`. **S**
- **App_SetParam** `0x0809FA50` / **App_GetParam** `0x08099504`. Sequence keys go to layer 0 for `0x46`/`0x49`/`0x6B`/`0x7A`/`0x88`/`0xB7`/`0xB9`/`0xBE`/`0xC0` and to the active layer otherwise. Global keys (kind 0x40) save `settings.tml`. **S**
- **Persistence:** preset XML and `settings.tml` write every bag row as `name=value` (`FUN_0808FA28`); load runs the template, then sets the attributes present. **S** for settings; **L** for sequence bags.
- **Encoder → list stepping:** `FUN_080B0B74` keeps a struct at slider+0x270 (int64 accumulator, divisor 400 for lists, index, count, wrap) and steps one entry per 400 accumulated units. **S**
- **3.1.Q params:** `0x19D` Rec Len, `0x19E` Seq Default, `0x19F` Rec Len Set (hidden), `0x1A0` Loop BPM, `0x1A1` Dbl Copy. Registry has 500 slots, so ids up to `0x1F3` are free to add. **S**
- **Page lists:** `InfoPage_Build` `0x08098D40` walks ushort id lists (pad pages 0x48 bytes per page; sequence page 0 at `0x080EEF28`; layers `0x080EEE98`; Tools `0x080EED18` + page × 0x48). **S**

## Sequencer

Details: [../map-319.md#sequence-recording-31q-custom-rec-len](../map-319.md#sequence-recording-31q-custom-rec-len).

- **Sequence objects** (UI): `FUN_08097CE8(app, {0x080EAF38, pad|0x100}, layer)` = app+0x1C00 + row·0x370 + col·0xB0 + layer·0x2C. **S**
  - Layer 0 holds `0xC0` active layer, `0x88` step mode, `0xBE` play, `0x46` quant.
  - Each layer has `0x85` step length (index into floats at `0x080ECA6C`, beats) and `0x86` step count (1..512 after the 3.1.Q patch).
- **Events** (0x18 bytes, list at layer+0x18, max 512): {byte0 1 / origin, position, length, handle (App+0xE808++), note | vel<<16, 0}. **S**
  - Positions are steps×960 in step mode, ticks otherwise.
  - Calls: get `FUN_08063D08`, add `FUN_08063B28`, update `FUN_08063B38`, delete `FUN_08063C08`, clear `FUN_08063CC0`.
- **UI → engine:**
  - push a layer `FUN_080985A0`; add/update one event `FUN_0804C798`; delete `FUN_0804C7E8` (handle 0 = all);
  - Double `FUN_0809C5B4`; undo snapshot `FUN_08098254`; clear `FUN_0809F820`; clear all `FUN_0809843C`; play toggle `FUN_080983D0`; select `FUN_08098328`; record-start launch `FUN_08098288`. **S**
- **Engine:** SE = `*(0x2400A9C0+0x9A74)`. **S**
  - Transport: SE+0x30 song ticks (960 per beat, 3840 per bar), SE+0x38 playing, SE+0x18 float BPM; anchors at SE+0x08/+0x10; UI copy via `FUN_0804CA68`.
  - Each pad's player re-reads `0x86` every step and loops on the quant grid; launch quantise is `0x46` (player+0xCB0).
- **Messages:** play `0x74` (`FUN_08051EB8`, lead-in only while REC is held, SE+0x128D4); REC-held `0x73`; stop `0x75`; record on/off `0x60`/`0x61`; launch `0x68`; tempo `0x20` (milli-BPM, fractional, re-anchors); BPM param `0x84` (integer 40–250). **S**
- **Recording:**
  - REC toggles recording only while the transport plays (`FUN_080524CC`). The UI learns through `0x66` → `FUN_0809AC60`.
  - Notes reach the UI through the ring at SE+0xEFF8 (0x30-byte entries: type 5/6, note | vel, engine key, song ticks at +0x28; +0x20 unused). They arrive even while stopped and are recorded only by `FUN_080A02CE` while App+0x8CC1 == 1. **S**
  - Tap tempo: param `0x19A` → `FUN_0806A428`. **S**
- **Step length table** `0x080ECA6C` (floats, beats), by `0x85`: 0 8 bars, 1 4 bars, 2 2 bars, 3 1 bar, 4 1/2, 5 1/2T, 6 1/4, 7 1/4T, 8 1/8, 9 1/8T, 10 1/16, 11 1/16T, 12 1/32, 13 1/32T, 14 1/64. A layer's length is `0x86` × table[`0x85`] × 960 ticks. **S**
- **3.1.Q length knob** (`seqtools.S` `seq_len_knob`): 400 encoder units per step like stock lists; list Custom, 1/8 … 32 bars stored as Rec Len; other lengths ×2/÷2 via `0x86` or a 2× step index, and ×2 first restores the step a ÷2 shortened (MX_STATE+76); ×2 copies the events (scratch App+0xE920, re-add at +old length with handle 0, push `FUN_080985A0`, post `0x66`), gated by global `0x1A1` Dbl Copy.

## Clip / Slicer engine

See [../map-319.md](../map-319.md#clip-control-panel-main--pos) and the Warp/stretch notes on the timestretch branch. Highlights:
- Clip object 0xBB8 bytes, two voices at +0x260/+0x670 (0x410 each). **S**
- Envelope at voice+0x258: gate on `FUN_0806A5F8`. **S**
- Modulation: route handler `FUN_08067018`, 3 slots per modparam, evaluator `FUN_0806FB30`. **S**
- Lane render `FUN_080667B8`; grain start `FUN_08065550`; slicer grain path `FUN_08065290` (10 arguments). **S**

## FX, EQ

- FX slots at App+0x2FC0 + i·0x18: Delay 0x26, Reverb 0x4B, EQ 0x36, empty. App+0x28 is the FX key shown (0x300 / 0x310 / 0x320). **S**
- EQ:
  - The graph is page+0x34, touch-mapped to its rect. Band n uses ids gain 0x118+5n, freq 0x119+5n, Q 0x11A+5n, enable 0x11B+5n, type 0x11C+5n (0 None, 1 L Cut, 2 L Shelf, 3 Param, 4 H Shelf, 5 H Cut). **S**
  - Layout `FUN_080AA8D2` runs on 0xD4. **S**

## Presets, files

See [../map-319.md#presetmgr](../map-319.md#presetmgr), [../preset-folders.md](../preset-folders.md). FatFs only from pcmStreamer; one shared sector window. **S**

## Tools and the bench

- `tools/bench/run_tests.py` runs patch hooks in Unicorn against a FAT card image. It includes a real-GUI test (`t_gui_fx_flow`) that builds the stock GUI and App and navigates screens with the draw loop.
- Emulator renders for screens: see the session scripts described in [../workflow.md](../workflow.md); logical y must be flipped for physical orientation.

## Open questions

- Who posts UI event 0xDE (player gate +0x51 via param 0x6E)? What is SE+0x144? **?**
- The audio block size, i.e. the resolution of SE+0x00 stamps. **?**
- Encoder physical order on hardware. **L**
- What the editor screen at GUI+0x45E40 does with encoders. **?**
- Whether every sequence bag row persists to the preset XML. **L**
