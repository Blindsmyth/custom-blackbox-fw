# Reference: j3threejay/blackbox-mod (build 3.1.n)

An independent MIT-licensed mod for the same stock 3.1.9 image (SHA-256 `281ae303…f1341d`), so every address below applies to our base unchanged. Cloned for reference to `reference/blackbox-mod/` in the main folder (commit `47f4312`, 2026-10-05). Their approach: compile C into a cave appended after the stock image (`STOCK_END` `0x080F1E78`, so it overlaps ours at `0x080F1E80`: the two mods can't be flashed together without relocating one cave) and redirect `bl` sites / table words.

Confidence: what they hook is **sure** (their patch table checks the stock bytes); behaviour notes are theirs, **likely** until we use them.

## Hook-site conflicts with 3.1.Q

| Address | Theirs | Ours |
| --- | --- | --- |
| `0x080A32DE` | MIX: three-way Mixer/Mute/Solo cycle | MIX stays on Mix |
| `0x0808BC40` | mixer set-mode: arm Solo | Mix always in mute mode |
| `0x080A43E0`, `0x080A43F8` | mute-mode fills: Solo colours | same (our Solo, ported from theirs) |
| `0x080F0F20`, `0x080F0F24` | mixer touch down / move | same |
| `0x080F07F0` | FX knob screen +0x10 (rebuild on Type) | FX Return page onEvent |
| `0x080EED18` | Tools Main list reordered (Comp Thresh) | Tools lists (we patch `0x080EED60` Rec page) |
| cave `0x080F1E78`… | their code | ours from `0x080F1E80` |

## RAM they found free

- `0x2405FF54..0x24060000` (172 bytes): alignment gap between the heap end and the MPU regions; not zeroed at boot. Heap is 200,000 bytes and `operator new` takes 167,704 in one block and **hangs on failure**, so patch state must never come from the heap (their build 3.1.U crash-looped that way).
- Backup SRAM `0x38800000` (4 KB): they keep audio-task DSP state there (overdrive `0x38800000`, compressor `0x38800400`, reverb `0x38800A00`). Our notes say backup SRAM needs `BKPRAMEN` + `PWR_CR1.DBP` before writes stick; worth re-checking against their working use. **open**

## Mixer (Mute / Solo)

- Mixer view = screens `0x2E` (Mixer) / `0x2F` (Mute), one widget. Mute-mode flag view+`0x1E40`, set by `FUN_080B5E44(view, mute)` which also calls `FUN_080A4F24(cell, mode)` on every cell (cell+`0x18A`).
- Cells: view+`0x3AC` + i·`0x1A0`, 16 of them. Cell+`0x38` pad id (col | row<<4), +`0x17C` dirty, +`0x189` unmuted, +`0x18A` mute-mode display.
- Hit test `FUN_080B5E78(view, point, &cell)`. Mute-mode touchDown `0x080B5F44` stores the target state (!unmuted) at view+`0x1E41` and calls touchMove (`0x080B5EBC`), which paints it onto every cell the finger crosses (view+`0x1E44` = last cell).
- Muting a pad: post `0x44 {+8 pad, +0xC unmuted}` up the widget chain (`FUN_080AED14`; App sends engine param 100), plus `FUN_080A4F10(cell, unmuted)` for the cell.
- Mute-mode cell fills: `bl FUN_0808EA22` (fill) at `0x080A43E0` (unmuted, green) and `0x080A43F8` (muted, red). Outline `FUN_0808E994`. Palette: `0x1C` dark yellow, `0x14` yellow.

## Keys screen and notes

- Keys key down (msg `0xFD`) → engine note-on `FUN_0804C61C` (msg `0x42`) at `0x080A0AA2`; key up (`0xFE`) → `FUN_0804C5D4` (`0x43`) at `0x0809B78C`.
- Keys view encoder 2 → `FUN_080B0B74` at `0x080AE178` (root stepper); root label refresh `FUN_080A3DEC` at `0x080ADA8C`.
- Scale lookup `FUN_0806E930`: entry = id, then one semitone offset per degree; count at +0x10.

## Sequencer

- Note condition ("PLAY", xml `cond`, one byte): 0 ALWAYS, 1–99 percent. Engine plays when cond = 0 or ≥ 100, else `rand() % 100 <= cond`. Registration `bl` at `0x0808DD98`; piano-roll event → knob `0x080AB028`, knob → event post `0x080AAC1A`; engine roll branch `cmp r3,#0x62` at `0x0805D3A6`, roll call `0x0805D51E` (`FUN_080C89D4` = rand).
- Note-seq object vtable `0x080D0744`: +0x58 add/update (`0x0805C0D8`), +0x5C delete (`0x0805C0F0`). Pattern list [2] = play index; sorted insert `FUN_08063938` bumps it when the new note lands at or before it, so a note dropped just ahead of the playhead only sounds next pass (their seqfix). Player `FUN_0805D1D8` (call at `0x0805D9A4`) plays the notes due in this block. Time-stretched sequences flag +0x5E. Step ticks `FUN_0805CAC0`. Clock position → sequencer time `FUN_08069C94`.

## Modulation (ducker)

- The engine has ten global modulation sources (mod1..mod10, CV inputs on the Eurorack sibling) that the Blackbox never feeds. Source list: names `0x080ECD0C`, codes `0x080ECCFC`, 8 entries (count in registration `0x0808CB3E`, loops `0x080B67CC`, `0x080B7994`); readers at `0x0808CCC0`, `0x080B6964`, `0x0809A69C`, `0x080B696C`, `0x080B7B00`.
- Per-block hook before the engine process `FUN_08053164` at `0x0804CE74`. Voice start: class A `FUN_08058548` (8 call sites), class B `FUN_08065908` (3 sites). Modulated param value `FUN_0806FB30`.

## Pad DSP

- Filter: per-voice `FUN_080762D8` (RBJ biquad, 12 dB/oct, LP left of centre / HP right, bypass at centre), calls `0x08055C60` (sample pads) and `0x080669D6` (clip/slicer). Filter object +0x84 → param block (Filter @+8, Res @+0x44). Curves: cutoff `FUN_08060640` (0..1 → Hz), res `FUN_080606C0` (→ Q).
- Interp list (Normal/HighQ) registration `0x0808D92A`. Sample reader `FUN_08055F0C` (4 sites) with kernels normal `FUN_080619D0`, HQ `FUN_08061A64`.
- Overdrive `FUN_0807081C`, once per pad per block after the voices are summed: pre-gain 2^(6·OD), lopsided table curve, −12 dB. Calls `0x08059AB6` (sample), `0x08068B12` (clip/slicer). Label literal `0x0808D878`.
- Engine pad id (pad+0x18) = bank<<16 | row<<8 | col. App pad record = App+`0x1840` + row·`0xF0` + col·`0x30`. Pad page lists (`FUN_08098D40`, 5 pages × 36 ids per mode): page 4 Sample `0x080EFD38`, Multisample `0x080EFBD0`, Slicer `0x080EF900`, Clip `0x080EFA68`.

## Master and FX

- Compressor: Tools param `0xB4`, engine stage `FUN_0804EDDC` once per 256-frame block on Out 1 (call `0x08053CA0`); registration `0x0808D350`; global defaults `0x08094886`. Tools Main list `0x080EED18`.
- Reverb (FX2): Dattorro plate at 29.8 kHz, object vtable `0x080D0814`, process `0x08062FE8`, core `FUN_08074F50(state, in, outL, outR, n)` (call `0x0806318E`); buffers at state+0x20 (4×512, 2×1024, 6×8192 floats, ~208 KB DTCM). Unused list param `0x198` "FX2" (Delay/Reverb, xml `fx2algo`). Reverb defaults `0x080944A2`.
- Delay (FX1): param `0x197` "FX1:" unused, registration `0x0808DE8E`; process wrapper call `0x08054F3A` (`FUN_080548C8`); delay defaults `0x0809444A`; settings-list fill `FUN_080A50FC` (call `0x08098E72`).
- App FX cell records App+`0x2FC0`, 0x18 each.

## System

- Audio task `FUN_08041470`: waits on a queue (`FUN_08088756`, FreeRTOS receive, call `0x080414E4`) for SAI DMA half/full, renders 256 frames (5.33 ms), waits again.
- Version string `0x080CF290` ("3.1.9"); global cell name `FUN_0809AD74` (address 0x40) is where the version shows on screen. Version labels: `0x080C54F8`, `0x080AEAB6`, `0x080C72A4`.
- Label text: `FUN_080A3DEC(label, s)` (text object at +0x34, redraw); text object copy `FUN_080C3B98` (≤ 31 chars, marks dirty).
- Knob/list widget: bind `FUN_080B1084`, label `FUN_080B03C0`, range `FUN_080B09E4`, reset `FUN_080B04C8`, type `FUN_080B0B08`, value `FUN_080B0790`.
- Slicer waveform: slices `FUN_080C6E8C`, select `FUN_080C6EC8`; slice get/set `FUN_08093CB4` / `FUN_08093CBC`; Slice Pos handler `FUN_080A53B8`; slicer setup unbinds the bottom knobs at `0x080A58F2` / `0x080A58FE` (`FUN_080A7EE0`); refresh `FUN_080A5210`.
