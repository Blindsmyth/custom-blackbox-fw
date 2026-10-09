# Roadmap

Planned features for the `mix-overhaul` line (3.1.Q and later). Addresses are stock 3.1.9; details of what's already mapped are in [map-319.md](map-319.md). Sizes: S (a hook or two), M (a feature `.S` plus bench), L (new behaviour on a timing path), XL (new engine DSP or engine-wide model).

## Done in 3.1.Q

- Mix screen:
  - pads play;
  - two four-fader layers (MIX held for the second);
  - knobs on their corner;
  - quick-touch blip from rest;
  - centre detent with snap on bipolar params;
  - MIX stays on Mix;
  - momentary INFO Mute.

## Done (3.1.Q, continued)

- **Batch 0:** encoders no longer select pads/sequences or switch Pads/Seq pages (all Pads knobs, Seq knobs 0, 1, 3). Parameter menus (row knob + value knob) stay stock.
- **Batch 1:** PADS/SEQS held + tap selects; SEQS held shows layers A–D; Seq bar label and double/halve; EQ tap-and-drag and default bands; FX toggles DJ FX ↔ Return with a bottom row A Delay / B Reverb / EQ / (empty).
- Fix: Mix fader labels repaint on every layer change; the MIX press that opens Mix isn't a hold.
- Holding PADS shows the CUT / COPY / PSTE / CLR panel (stand-in until the Batch 3 picker).
- Open: a crash reported on pressing FX again; not reproduced in the bench (DJ FX → Return → DJ FX with real GUI, FX slots and draw loop runs clean).

## Next, in order

### Batch 0 (done): encoders stop navigating

Encoders no longer select pads or sequences, and no longer switch pages on the Pads and Seq screens. Selection is PADS held + tap and SEQS held + tap. Parameter menus, where one encoder picks the row and the other changes it, stay as they are. The freed encoders do nothing until they're remapped.
- **Known navigation encoders:**
  - Pads onEvent `0x080B4134` idx 0/2/1;
  - Seq onEvent `0x080B5284` (slider `+0x1AB0`);
  - the INFO-page scroll lists (`0x080A86A0`).
- **Before disabling any list's encoder:** check the list can be scrolled and selected by touch. Don't strand a screen.

### Batch 1 (done): quick wins (UI)

| Feature | Hook | Size |
| --- | --- | --- |
| EQ: tap a dot, drag it | Graph vtable `0x080F066C` → pick the nearest dot (graph+`0x19D0`+i·`0x34`), `0x080AA03C`, post 0x6E `0x12E`, then stock touchDown `0x080A9B40` | M |
| EQ default bands L Shelf / Param / Param / H Shelf | `0x0809470E` `02 22`, `0x0809474E` `03 22`, `0x0809478E` `03 22`, `0x080947CE` `04 22` | S |
| SEQS held + pad = select only | Seq onChildEvent `0x080B4DEC`: post `0xFC` instead of `0xFA` | S |
| PADS held + pad = select only | Pads onChildEvent `0x080B3E84`: 3 → `0x63`, swallow 4 | S |
| Seq bar count above Undo | Seq+`0x3F70` button text, bars = `0x86 × table[0x85]` (`0x080ECA6C`) / 4 | S–M |
| Seq TR knob = double / halve (current layer only) | Double `FUN_0809C5B4`; halve: snapshot `FUN_08098254`, `0x86/2`, trim events, push `FUN_080985A0`, `FUN_0809C2C8` | M |
| FX button toggles DJ FX ↔ Return FX | `App_HandleInput` FX block `0x080A32A4`: from any screen or 0x23 → 0x37; from 0x37 → 0x23 (set App+`0x28` = `0x300`/`0x310`, last used). EQ 0x36 and the send page 0x30 leave the cycle. | M |
| Return page: Tools-style buttons Return A, Return B, EQ | Map the Tools button row first. A/B post `0x116` with `0x300`/`0x310` and rebuild 0x23. EQ calls `App_SetScreen(0x36)`. The fourth slot stays hidden. | M |

### Batch 2: sound

| Feature | Hook | Size |
| --- | --- | --- |
| Slices trigger the ADSR: new option at the bottom of the Slicer Conf page (after Pad Note `0xF5`) | Param like Warp (registry, bag template, XML). Gate-on `FUN_0806A5F8(voice+0x258)` after the slice-change `bl FUN_08065550` at `0x08066B9A` / `0x08066CF2` in `FUN_080667B8`, when env stage is 1–3 | M |
| ADSR → filter, Clip/Slicer | Route handler `FUN_08067018` at `0x08067046`: src 1 → `FUN_0806FCD4(amt, clip+0x448/+0x858, clip+0x4DC/+0x8EC, slot)`. Add "ENV" to the `modsource` enum (`0x080ECCFC`, `0x080ECD0C`, count in `Param_RegisterAll`) | M |
| ADSR → filter, Sample pads | Sample route handler `FUN_08056810`; map the sample voice envelope and filter modparam first | M |

### Batch 3: Pads side panel

- PADS held shows a picker for the right column (x `0x101`, 62 px). The functions:
  - **Vel Slide:** the VEL fader `+0x2144` (`0xCC`), full height;
  - **Beat Repeat**;
  - **Editing:** CUT/COPY/PSTE/CLR.
- There's no Performance function: the Mix screen is the performance surface.

### Batch 4: Beat Repeat

- Hold a pad: it retriggers at a rate from the Delay musical-time list (param `0x33`): 1/64, 1/32, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/2, 1 bar.
- The rate slider snaps to those 12 entries.
- It re-posts `Engine_PadPress`/`Release` (`0x0804C61C`/`0x0804C5D4`) on the clock: 960 ticks per beat, tempo from `0x84`.
- Risks: jitter, and the 64-deep pad queue (`FUN_0804F504`). L.

## Later

### Free Seq Rec (Ableton-style clip recording)

- **Toggle:** "Free Rec: Off/On" in Tools → Rec, next to `0x98 recquant`, `0x97 recpresetlen` and `0xD5 reccountdown`.
- **Only when the selected sequence is not playing.** If it's playing, REC keeps the stock behaviour.
- **REC press** arms. On the next bar it starts that sequence playing and recording (msg `0x60`), with free length: `FUN_080A02CE` skips its modulo wrap so positions keep growing.
- **REC again** (msg `0x61`): set `0x86 notestepcount` to the recorded length rounded up to whole bars, push with `FUN_080985A0`. The sequence keeps looping.
- **Quantise:** recorded notes use `0x46 quantsizeseq`, shown on the Seq screen.
- **Unknowns:**
  - the Tools → Rec page id list;
  - launching one sequence on a bar boundary (`FUN_080983D0` toggles `0xBE`, engine `FUN_0804C87C`);
  - the engine `0x60`/`0x61` handlers;
  - growing `0x86` during play.
- Size: L.

### Time signatures

- Stock has no meter: 1 bar = 4 beats is built into the step-length table (`0x080ECA6C`) and section math (`FUN_0809C2C8`). "TimeSignature" strings exist only in the Ableton XML parser (`FUN_0808A380`).
- Wanted: the Song screen's lower two buttons set numerator/denominator. Which buttons are meant is still open; the right column is hidden outside section edit.
- **Bars-only version (M):** bar display, bar length for double/halve and recording.
- **Engine-wide version (XL):** metronome accents, quantise, song bars.

### FX return High Cut / Low Cut

- None exist. Delay has a band filter: Cutoff `0x0E` + Width `0xCA` (+ enable `0xC9`). Reverb only has Damping `0x3A`.
- Mapping HC/LC onto Delay's band filter is M–L; real filters on both returns are XL (new DSP).

### Second envelope

- Needs per-voice state outside the clip struct (stock uses it through `+0xBB0`), a tick in the voice render, new params and UI. XL.

### Encoder remap

- Decide what the freed encoders do after Batch 0.

## Decisions

- No Performance side function; Mix covers performance.
- Seq double/halve acts on the current layer only, like stock Double.
- The Beat Repeat rate slider snaps to the 12-entry rate list.
- The FX button only toggles DJ FX ↔ Return FX. EQ is reached from the Return page's third button; the fourth button stays hidden.
