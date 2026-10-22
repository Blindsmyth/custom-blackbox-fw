# Song cues: Clip Deck hot cues per Song scene (design, 3.1.T)

Status: built in 3.1.T (design approved 2026-10-22, cue letters moved to the pad grid the same day), bench only. Not yet tried on the unit. How to use it: [lineup/README.md](../firmware/patches/3.1.9-mix-bungee/lineup/README.md#song-cues).

Goal: a Song scene (section) can send Clip pads to a hot cue when it starts, the way a cue note would, without any note passing through the pad's Toggle/Gate handling.

## What stock does (3.1.9, traced)

**Scenes are layers of one sequence.**
- Up to 32 scenes. Each scene object is 0x30 bytes at `app + 0x29C0 + i·0x30`: a param bag at `+0`, the event list at `+0x18`, the name at `+0x2C`.
- The scene count is param `0xC1` in `app + 0x3038`. Play Count is `0xC3` in the scene's own bag. The scene template (type `0x4E`, `Param_BagTemplate` case at `0x08094506`) adds only `0xC3`.
- A scene's events are `sceneitem` events (type 9). Word 0 holds `type | cond << 8 | chan << 16`, and `silayer` sits in word 5 bits 16–31.
  - `chan` = pad 0–15 is that pad's **sequence**: `silayer` is the pattern A–D, `cond` is 0 off, 1 on, 2 Keep.
  - `chan` = `0x100 | pad` is a **pad event**: the pad is started or stopped at scene start.
  - A new scene gets 32 items, both kinds for all 16 pads, all off (`FUN_08094C18`).
- **XML.** Each scene is a `<sequence>` with `<params>` holding every bag entry, written by name (`FUN_08095328`). After that comes one `<seqevent type="sceneitem" chan silayer cond step>` per item (`FUN_0809557C`).
  - On load, the template is applied first and then only template params are read back (`FUN_08095910`). A param can therefore only be saved by name if the template has it.
- **UI to engine.** `FUN_0809C140(app, i)` re-sends scene `i` to the engine sequence at key `0x20303`, with layer = `i`. It sends a clear first, then each item (`FUN_0804C798`), then every bag param (`FUN_0804C59C(engine, 0x20303, id, value, 0, i)`).
  - Add, Cut, Paste, Delete and Clear call it for every scene they move. `FUN_0809C2C8` sets each layer's step length and count (`0x85`/`0x86`): bars × Play Count.
- **Engine.** The generic sequence player (`FUN_0805D8C4`) calls `FUN_0805CDA0` on every step.
  - At step 0 of the playing layer (a scene start, and every Loop1 repeat), it walks the type-9 items. For each one that is on, it sends `0x8B` (start) or `0x8C` (stop) to that sequence or pad (`FUN_0805F460`). Keep items send nothing.
- **Song screen** (`FUN_080C2980`, vtable `0x080F1758`, handler `FUN_080C3540`). Event `0x2B` opens a scene's **sequence grid** (mode 2), and `0x2C` opens its **pad grid** (mode 3).
  - In both grids the right column `{x 256, y 0, w 62, h 219}` (`FUN_080B8548`, Song + `0x11E28`) holds 5 buttons, top to bottom on screen: **A, B, C, D, Keep** (ids `0x18C` … `0x18F`, `0x190`, each 43 px high; A at y 176, Keep at y 0, as the screen's y axis runs upwards). In the pad grid A–D are hidden.
  - A press sets the "brush" at Song + `0x1218D` (A–D = 0–3, Keep = 4).
  - A cell tap (`FUN_080C2500`) posts UI event `0xDC` to the app, which edits the scene's item.
  - The 4×4 grid (Song + `0x1072C`, 172 px square at x 43, y 5) shows one letter per cell (`FUN_080A367C`) plus a Keep mark (`FUN_080A36A8`). Both are refreshed by `FUN_080C2890`.

## Proposal

### UI: the pad grid works like the sequence grid

Revised twice on 2026-10-22 after review. The first build put `CueA`… columns in the sequence grid, upside down (the screen's y axis runs upwards). The cues belong with the pads, and the pad grid should work the way the sequence grid does.

- **Brushes.** The scene's **pad grid** (mode 3) gets **Def** and **A–H** next to stock's **Keep**. Def is stock's plain pad event. A cue letter is a pad event that starts the pad at that cue.

| x | Column (top to bottom) |
| --- | --- |
| 194 | **A, B, C, D, Def** |
| 256 | **E, F, G, H**, stock's **Keep** (E–H take the places of stock's A–D, which the pad grid hides) |

- **Taps, as in the sequence grid.** Pick a brush, then tap pads. An off pad goes on with the brush, so different pads can take different cues. An on pad goes off and loses its cue. To change a pad's cue, tap it off and on again with the new letter.
- **Cells.** The grid now draws letters in the pad grid too. A pad with a cue shows its letter, a Def pad is lit with no letter, and Keep shows stock's arrow.
- **Size.** Every column is stock's 62 px. The grid keeps stock's 172 px (it moves to x 12), because the screen's height decides its size.
- **What stays stock.** The sequence grid and the small grids on each scene row are unchanged. The row grids show no cue.
- Cut / Copy / Paste / Add / Delete carry a scene's cues with it, because they are in its bag. **Clear** removes them along with the scene's actions.

### What a scene's cue does

When a scene starts in Song mode (the song player's step 0 for that scene, including each Loop1 repeat), every pad with a cue in that scene is handled like this:

| Pad state | What happens |
| --- | --- |
| Clip pad, playing | The jump is pending and is taken at the next **Quant Size** boundary, exactly as a cue note sets it (`DkCue.pending`, taken in `deck_wrap_unit`), with the same prefetch. |
| Clip pad, stopped | The cue is **armed**: the next time the pad starts during this scene (its pad event, its sequence, a pad press), it starts at the cue. The next scene start replaces or clears it. The cue alone does not start the pad. |

Nothing happens when:
- the pad isn't a Clip, or has Sync None / Slice;
- its cues are not in use (Cue Mode off and no cue set);
- that cue letter is Off on the pad.

The jump is called straight from the song player into `deck.c`. No note is generated, so Toggle/Gate, MIDI In channels and Cue Note play no part.

### Storage: by name, in the scene's bag

- 16 new params `scue1` … `scue16` (one per pad, ids `0x1C0`–`0x1CF`; 3.1.T uses up to `0x1BF`). Values: 0 = none, 1–8 = Cue A–H.
- They are added to the scene template (hook at `0x0809450C`, the `0xC3` add). Stock code then saves them by name in each scene's `<params>`, reads them back on load, and copies them with the scene. Stock 3.1.9 ignores them, and a stock preset loads with no cues.
- **Engine copy.** A hook on `FUN_0804C59C` catches key `0x20303` with our ids. It writes them to an SRAM4 table, 32 scenes × 16 pads at 4 bits = 256 bytes, and does not forward them to the engine. The table is therefore refreshed every time stock re-sends a scene (load, edit, add / delete / paste), with the same scene index as the engine layer. The song player hook reads it.

### As built

Changes to the plan above, and what was confirmed:
- **Armed for the whole scene.** A stopped Clip starts at the cue every time it starts during the scene, not only the first time, so a sequence that retriggers the clip keeps the cue. Stopping the song clears it.
- **A playing Clip is armed too**, so its own sequence retriggering it in that scene starts at the cue.
- **The scene's own pad event on a playing Clip** (stock's msg `0x8B`, a voice start with note 0) would retrigger it. While the scene's jump is waiting for that pad, `deck_cue_note` turns that start into the pending jump instead, so the clip jumps at the next Quant Size boundary like a cue note. A stopped Clip is started by the pad event as stock does, at the cue.
- **A cue note after the scene start wins** over the scene's cue for that pad.
- **The next scene's cues are kept cached** while a scene plays (`song_grain`, the same block request as a pending cue note), so the jump at the scene start is gapless. Next = the following scene, the first after the last, the same one on Loop1. A scene queued by hand is not known ahead, and its jump may not be cached.
- **Song player.** The song is the sequence player with `+0x60` set and key `0x20303` at `+0x18` (created in `FUN_080525F0`). `+0xC20` is the playing layer, layer `+0x40` its index (the scene), `+0x64` the scene count, `+0x5C` set when the song moves on (not Loop1). `FUN_0805D7E0` switches the layer before `FUN_0805CDA0` runs step 0, so the hook sees the new scene. Hook: `0x0805CE0C`, after stock's own checks (step 0, playing, song).
- **Pads.** Scene cell k is pad k: the engine's pad keys (`0x240151D0`, set in `FUN_0805F128`) are `row << 8 | col` for k = row × 4 + col, the same index Clip Deck uses.
- **Engine copy.** `hook_post` (Engine_PostParam, `0x0804C59C`) gets the layer as its 6th argument; our ids are stored and never posted.
- **Memory.** `SgState` at SRAM4 `0x38006400`–`0x38006BDC`: free on 3.1.T, as the Looper ends at `0x38006400` and RbVoice starts at `0x38007000`. 3.1.Y's slice pool (`0x38006000`) is not in this build.
- **UI hooks.** Song vtable `0x080F1758` `+0x30` (`FUN_080C3540`) and `+0x34` (`FUN_080C3860`, the 0x66/0x67 refresh) are wrapped by `song_event` / `song_event2`. The panel layout call at `0x080C323A` in the constructor goes to `song_panel`. Our buttons are ids `0x191`–`0x198`, children of the A–D panel.

### Not checked on the bench

- **Downbeat order within a block.** Does the song player run before the Clip lanes in the same block? If not, a playing clip's jump at a scene start waits for the next Quant Size boundary. The bench runs the two separately. Listen for it on the unit.
- **Drawing.** Nothing is drawn on the bench. The buttons' places, the hidden stock A–D and the cells' chars are checked from the widget rects and flags, not on screen. A Def pad's cell is a space char with letters on, which should look like stock's lit cell.

## Decisions (2026-10-22)

The user approved the proposal: a stopped Clip is armed rather than started. After the first build, the user moved the cues to the pad grid, working like the sequence grid: brushes Def and A–H, A at the top (the first build had them bottom-up).
