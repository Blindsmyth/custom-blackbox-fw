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
- **SRAM4 in 3.1.Q:** `0x38000000` folder state (`0x4930`), `0x38005000` Warp flags, `0x38005200` MX_STATE (`'MIXU'`, +0..+76), `0x38005400` EQ page buttons (4 × 0xA0, to `0x38005680`), `0x38005110` Env Trig table (magic + 0x50 bytes by row<<2|col, `adsr.S`; save slots at +4), `0x38005700` Rec Len own-length label (16 bytes), `0x38005800` looper note buffer. **S**
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
- **Mixer view (Mix / Mute, screens 0x2E / 0x2F):** see [blackbox-mod.md](blackbox-mod.md#mixer-mute--solo) for the cell layout, mute message `0x44` and hooks. 3.1.Q keeps the view in mute mode on Mix (`mixmute.S`). Stock MIX (`0x080A32DE`) picks 0x2E or 0x2F from that mute flag, so our hook decides from the current screen byte App+`0x8CA4` instead; MIX on Mix is momentary in `mix_tick` (MX_STATE +112 level). touchDown routes MIX-held taps to select (`0x63`), Mute to stock, Solo to ours; INFO (`0x080A30DC`) cycles the pad mode. MX_STATE +92..+104. **S**
- **Header bar** (one widget, built by `FUN_080AB614` from `0x080AB4D4`; vtable `0x080F077C`, tick `FUN_080AB78C`): labels at +0x34 counter "%d:%d" (x 10 w 100), +0x8C "%dMB" / "FULL" (x 80 w 48, right-aligned), +0xE4 (x 80 w 200, never written by stock), +0x13C "Seq %d" / "Seq %d: %c" (x 140 w 140), loop bar +0x194 (x 256 y 224 w 63 h 12); all y 222 h 20. **Its owner (built at `0x080AB4B8`: screen at +0x34, header at +0x3C) calls the header tick directly at `0x080AB594`, so a vtable hook on `0x080F077C` never runs.** Status from `FUN_0809BBF8(app, out)` (+0 counter, +8 text, +0x20/+0x24 loop position / length). Label rect at label+4. 3.1.Q moves Seq to x 170 w 84 and uses +0xE4 (x 90 w 36) for "Sel" and narrows the counter to w 70: a label repaints its whole rect (background) whenever its text changes, so overlapping rects wipe each other (the counter changes every beat). Text is 12 px per character (`FUN_0808ED28`); label +0x51 alignment 1 left / 2 centre / 3 right, +0x53 colour while selecting. **S**
- **Reference mod:** [blackbox-mod.md](blackbox-mod.md) collects the addresses j3threejay/blackbox-mod hooks on the same 3.1.9 image (Keys chords, note conditions, ducker mod sources, filter/interp/overdrive, compressor, reverb/delay, audio task, free RAM). **S/L**
- **3.1.Q params:** `0x19D` Rec Len, `0x19E` Seq Default, `0x19F` Rec Len Set (hidden), `0x1A0` Loop BPM, `0x1A1` Dbl Copy. Registry has 500 slots, so ids up to `0x1F3` are free to add. **S**
- **Settings rows (INFO page class, vtable `0x080F0004`):** a row builder calls `Param_Labels` at `0x080A7F20`, then slider set-labels `FUN_080B0998(slider, labels, count)` (slider+0x158 labels, +0x154 count) and set-value `FUN_080B08F4(slider, value, force)`, which copies `labels[value]` into the slider text (+0x9C, `FUN_080C3B98`) only when the value changed or force is set. The page onEvent `0x080A6D28` handles `0x66` for its own key (page+0x29C == event+8) by re-setting every row (`FUN_080A54A0` → `FUN_080A7FE6`, force 1), so a label pointing into RAM picks up new text on each refresh. 3.1.Q uses this for Rec Len value 10 (`SR_TXT` SRAM4 `0x38005700`). **S** (refresh path read from code; device check pending)
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
  - **Ring gate (engine):** the sequence player's note-on `FUN_0805C810` pushes a type-5 entry only when player+0x51 && player+0x5F. Player params (switch at `0x0805EBDA`, ids 0x45–0xC2): `0x6E` → +0x51 "record target", `0x70` → +0x50, `0x6B` → +0x44, `0x64` → +0x41, `0xB7` DispMode → +0x59 = (value ≠ 2) and +0x5F = 1. The engine sends `0x6E` = 1 to the selected sequence (SE+0x29C) only when recording starts with the transport playing (`0x08050724`, `0x08050DA8`: needs SE+0x38 and SE+0x143 or +0x144) and 0 when it stops or the selection moves. So with the transport stopped no notes reach the ring unless the UI sends `0x6E` = 1 itself, as the 3.1.Q looper does. Param send helper: `FUN_0804C59C(eng, engine key, id, value, a4, a5)` = msg `0x39` {+4 a4, +8 key, +0xC id, +0x10 value, +0x14 a5}. **S**
  - **Note routing (engine):** notes reach a sequence player as message `0x8E` (on; case at `0x0805E8AA` → `FUN_0805C810`). The engine's selected sequence is SE+0x29C, set by global param `0xAF` (key 0x400; the UI sends it on select, `FUN_08098328`); SE+0x2A0 is its index in a 20-entry table at SE+0x150 (16 bytes each: +0x150 PADS, +0x151 KEYS, +0x152 MIDI flags from DispMode `0xB7`, set at `0x080505C0`; +0x154 / +0x158 key). Touch pads (`FUN_0804F744`) are forwarded only in PADS or KEYS mode; MIDI-in (`FUN_0804FE18`, msg 5) by channel tables (SE+0x24 + i·12, SE+0x348 / +0x34C). **S**
  - **Queued sequences:** with the transport stopped, layer 0's `0xBE` = 1 means "starts with PLAY"; stock REC then goes record-ready (LED blinks) and waits for PLAY. **S**
  - **Ring pop:** `App_Update` calls `FUN_0804C754(ring, &entry)` at `0x080A2936` in a loop until it returns 0, so its last call per update is a free per-loop tick. 3.1.Q's looper wraps it (`looper.S`). Entry +0 type (5 on, 6 off, 7/8 playback), +2 origin, +0x10 note | vel<<16 (off: note), +0x18 op = 1, +0x1C sequence engine key, +0x20 free, +0x24 target pad key, +0x28 song ticks (frozen while stopped). **S**
  - **Time base:** SE+0x00 u64 sample counter (48 kHz), advanced every block, playing or not. Tempo anchor SE+0x08 ticks / SE+0x10 samples / SE+0x18 float BPM; ticks = ticks0 + (samples − samples0)/48000 × BPM/60 × 960 (`FUN_0804D708`). **S**
- **Engine commands:** `FUN_08051738(SE, msg)` copies a 24-byte message (byte 0 id, +8 value) into the command ring at SE+0xD7E0 (index SE+0xDDE4). `0x20` tempo in milli-BPM (handler `0x08052220` → `FUN_08069BEC`, re-anchors, ignored but stored at SE+0x13C under external sync); `0x73` REC held (+8); `0x74` play (`FUN_08051EB8`: lead-in and recording only while REC is held); `0x75` stop. `FUN_0804C8B4(eng, rec, stop, play)` wraps 0x73/0x74/0x75. Tempo display: `FUN_08097C1C(app, msg{+0xC milli-BPM})` sets `0x84` (truncated), posts 0x66, marks the preset dirty. **S**
- **Start a sequence at transport start:** `FUN_08098288(app, ref)` = `0xBE` 1 + msg `0x68`; the engine's transport-start broadcast `0x57` starts every player with +0x5B set at tick 0. **S**
- **Step length table** `0x080ECA6C` (floats, beats), by `0x85`: 0 8 bars, 1 4 bars, 2 2 bars, 3 1 bar, 4 1/2, 5 1/2T, 6 1/4, 7 1/4T, 8 1/8, 9 1/8T, 10 1/16, 11 1/16T, 12 1/32, 13 1/32T, 14 1/64. A layer's length is `0x86` × table[`0x85`] × 960 ticks. **S**
- **3.1.Q length knob** (`seqtools.S` `seq_len_knob`): 400 encoder units per step like stock lists; list Custom, 1/8 … 32 bars stored as Rec Len; other lengths ×2/÷2 via `0x86` or a 2× step index, and ×2 first restores the step a ÷2 shortened (MX_STATE+76); ×2 copies the events (scratch App+0xE920, re-add at +old length with handle 0, push `FUN_080985A0`, post `0x66`), gated by global `0x1A1` Dbl Copy.

## Clip / Slicer engine

See [../map-319.md](../map-319.md#clip-control-panel-main--pos) and the Warp/stretch notes on the timestretch branch. Highlights:
- Clip object 0xBB8 bytes, two voices at +0x260/+0x670 (0x410 each). **S**
- **Envelope** at voice+0x258: +4 stage (0 idle, 1 attack, 2 decay, 3 sustain, 4 release, 5 done), +8 settings, +0x28 raw ADSR level 0..1 (ENV source reads it as pointer env+0x24, +4) (written every block by the process `FUN_0806A6C0(env, buf, mode, n)`), +0x1C smoothed level applied to the audio, +0x34/+0x38 velocity scaling. Gate on `FUN_0806A5F8` (resets +0x1C to 0, or 1.0 with zero attack), note off `FUN_0806A63C`, quick release `FUN_0806A698`, reset `FUN_0806A5D8`. **S**
- **Modulation routing** `FUN_08067018(clip, msg)` (only caller `bl` at `0x080685C4`): msg +0xC target id, +0x10 slot, +0x14 source code, +0x16 amount (s16). It binds a modparam slot to a float pointer with `FUN_0806FCD4(amount, modparam, source, slot)` (modparam +8 + slot·8 = source pointer, +0xC + slot·8 = amount); the engine reads the pointer every block, evaluator `FUN_0806FB30`. VEL (0xD) binds both voices' modparams to per-voice floats (clip+0x2A8 / +0x6B8); other sources bind voice 0's modparam to a pad-level float: LFO 4 → +0x388, 0xE → +0x2B0, 0x19 → +0xB60, 0x1A → +0xB48, 0x1C → +0xB50, 0x1D → +0xB58, the rest → +0xB70. Targets (voice 0 / voice 1 modparam): 0xC8 Cutoff +0x448/+0x858, 0x11 +0x484/+0x894, 2 and 4 +0x2B8/+0x6C8, 0x3C +0x514/+0x924, 0x3E +0x548/+0x958, 0x40 +0x57C/+0x98C, 0x62 Pan +0x26C/+0x67C; 0x4E and 0x5C special. **S**
- **Source list** (param 0x53): codes `0x080ECCFC` (u16), names `0x080ECD0C`, 8 entries (none, VEL 0xD, LFO 4, PTCH 0x19, MODW 0x1A, MVOL 0x1C, MPAN 0x1D, CC 0x8000). 3.1.Q adds ENV (code 0x30, `adsr.S`): routed like LFO (the pad-level path binds voice 0 and, through a jump table on the source code at `0x080671F6`, voice 1 for every target; VEL's path has no Pitch), then each voice's slot points at its envelope (clip+0x4DC / +0x8EC = env+0x24): the evaluator `FUN_0806FB30` sums *(float *)(source + 4) · amount over the active slots (+0x20 index bytes, +0x24 count), then clamps to [+0x28, +0x2C] into +0x30, so a source pointer addresses an object whose output is at +4. ENV is per voice. Pitch 0x5C modparams: +0x2F4 / +0x704. **S**
- Lane render `FUN_080667B8(clip, ?, voice, ...)` (one call per voice, `bl` at `0x0806887C`); grain start `FUN_08065550(clip, voice, …, 3 stack args)`, slice-change calls at `0x08066B9A` / `0x08066CF2` (and `0x08065A9A`); slicer grain path `FUN_08065290` (10 arguments). Env Trig = Slice (pad param 0x1A2) restarts a held voice's envelope there. **S**

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
