# Seq MIDI out: MPE and Port per sequence (3.1.U)

Status: built and bench-tested on the lineup image, **not yet tried on the unit**. The menu reads `3.1.U` (3.1.T plus this). Code: `firmware/patches/3.1.9-mpe-out` (`mpe.c`, `mpe.S`, `sites.py`), chained into `../3.1.9-mix-bungee/build_patch.py --lineup`.

A sequence whose **MIDI Out** is on (any channel) can send its notes Ableton-style for an MPE synth, one note per channel. Each sequence can also choose its output port.

## How to use it

Seq screen → the sequence's own page (where **MIDI Out** and Rec Len are). Under Rec Len are four new rows, one setting per sequence (all its layers):

| Row | Values | Default | What it does |
|---|---|---|---|
| **Port:** | Both / DIN / USB | Both | Where the notes go. Both is stock. **USB** still needs Tools → **USB Dev Out** on, as stock. |
| **MPE:** | Off / Lower / Upper / Multi | Off | Off is stock: every note on the MIDI Out channel. |
| **Last Ch:** | 1–16 | 16 | The last note channel. The first is the sequence's **MIDI Out** channel, as Ableton's First / Last Note Channel. |
| **PB Rng:** | 0–96 | 48 | Pitch bend range of the member channels, in semitones (the MPE default is 48). |

Note channels are MIDI Out … Last Ch, either way round, without the zone's master:
- **Lower:** master channel 1. MIDI Out 1 and Last Ch 16 give notes on 2–16 (Ableton's default).
- **Upper:** master channel 16. MIDI Out 1 and Last Ch 16 give 1–15; MIDI Out 15 and Last Ch 13 give 13–15.
- **Multi:** no master. MIDI Out 1 and Last Ch 16 give all 16.
- With only the master in range (Lower, MIDI Out 1 and Last Ch 1), notes go out as stock.
- **MIDI Out must be on**: stock sends no notes without it.
- They are saved in the preset as `midiport`, `mpemode`, `mpelast` and `mpebend`. Presets without them get the defaults; stock 3.1.9 ignores them.

**Channels:** a new note takes the member channel that has been free longest (least recently used). When every member channel is busy, the oldest note is cut: its note-off goes out first, then the new note takes its channel. A note-off always goes to the channel its note-on went out on. Two notes of the same pitch get two channels, and the first off ends the older one.

**MPE setup:** before the first note after any change of mode, MIDI Out, Last Ch, PB Rng or Port (and after a preset load), the sequence sends:
- Lower / Upper: the MPE Configuration Message (RPN 6 = N, the zone reaching the farthest note channel) on the master channel, then RPN 0 = PB Rng on each member channel and RPN 0 = 2 on the master.
- Multi: RPN 0 = PB Rng on each member channel.

Each RPN is CC 101 0, CC 100 n, CC 6 value, CC 38 0, then the null RPN (CC 101 127, CC 100 127). Lower with 15 channels is 102 messages (about 100 ms on DIN at the first note); Multi is 96.

**Changing Port or MPE while notes hold** ends them first, as stock does when the MIDI Out channel changes.

## The stock code it uses

### The note path

Sequences have 16 engine players (`0xCB8` bytes, ctor `FUN_0805C518`, vtable `0x080D0744`). They are built in the engine graph builder `FUN_080525F0`, with key `0x1RRCC` (row, column 0–3), next to one more player with key `0x303` and a node of type `'A'` with key `0x400`. A player's MIDI-out notes are engine messages:
- `+0` type: `0x50` on, `0x51` off;
- `+8` byte = `player+0x68`;
- `+0xC` = `player+0x69`, the MIDI Out channel (`0x6B` − 1);
- `+0x10` note;
- `+0x14` velocity (on).

They are posted to the router's MIDI queue (`router + 0x15238`) from six places, all reading `+0x68` and `+0x69` at the time they post:

| Function | What |
|---|---|
| `FUN_0805CEF4` | a step's note-on (`Player_StepNoteOn`) |
| `FUN_0805D070` | its note-off at the note's end (`Player_StepNoteOff`) |
| `FUN_0805D56C` | all-off for the held list (`player+0x70`, 16 bytes × `+0x270`): stop, channel or port change |
| `Player_Tick` `0x0805D8C4` | the same, inline, when a queued layer takes over (`+0xC24`) |
| `Player_NoteOn` / `Player_NoteOff` `0x0805C810` / `0x0805C970` | live notes through the sequence; held in `player+0xC28[note]` |
| `0x0805E0F8` | all-off for those live notes |

pcmStreamer's `Pcm_PollMidi` (`FUN_0804CB1C`) builds the bytes for `0x50`–`0x53` (the jump table at `0x0804D02C`; nothing else reaches the call) and sends them all from one call, `bl FUN_08044A0E` at **`0x0804D004`**. The message is at sp + `0x48` there.

- `FUN_08044A0E(mgr, packet, din, usb)`. The packet is 3 bytes plus a length byte at +4.
- Notes take DIN from `pcm+0xA68B` and USB from `pcm+0xA68D` (USB Dev Out). Clock messages take `+0xA68C` / `+0xA68E`.
- `0x52` / `0x53` (bend, CC) are never posted by a player.

### Params

`midiport` (`0x74`, "MIDI Port:", int 1–4) is registered by stock at `0x0808CDA2` but is in no template. The player's param switch (`0x0805EEF0`) stores it in `player+0x68`, with an all-off first if it changed. Nothing reads `+0x68` except the six senders above, and nothing in stock posts `0x74`, so `+0x68` is always 0 in stock.

`0x73` "MIDI Ch:" and `0x75` "Bend Rng:" are the same kind of dormant ints. The key-`0x400` node keeps its own port and channel at `+0x294` / `+0x298`, and `FUN_08061E60` is its note-on. It is not a sequence, so this patch leaves it alone. Its port values (1–4) are below the tags (see below).

**The player's param switch:** in the player's event loop (`FUN_0805E58C`, vtable `+0xC`, audio task), each `0x39` message whose key matches the player is at sp + `0x18`:
- `+0xC` id;
- `+0x10` value;
- `+0x14` the layer it was posted for.

`tbh` covers ids `0x45`–`0xC2`; everything else goes to `0x0805ED38`, which hands `0x85` / `0x86` / `0x49` to the per-layer entries (`player+0x318`, 32 × `0x48`, layer at `+0x48`). `0xC0` (active layer) is `FUN_0805E364(player, transport, layer)`.

**Where layer params live:**
- Layer A is the sequence bag (template case `0x47`). Layers B–D are layer bags (case `0x48`; stock: `0x85`, `0x86`).
- The session's four bags per sequence and the app's id list for a layer push (`app+0x8DD8`) are all built with `0x47`.
- `App_SetParam` sends every id outside the layer-0 set (`0x46`, `0x49`, `0x6B`, `0x7A`, `0x88`, `0xB7`/`0xB9`/`0xBE`/`0xC0`) to the active layer's bag, and posts it with that layer's index.
- A layer push (`FUN_080985A0(app, ref, layer)`, also for every layer at a preset load) posts all of a layer's ids with its index.
- So the engine gets every layer's value, tagged with its layer, and must keep them apart itself.
- A layer page is shown by making that layer active (Tools-style page change posts `0xC0`).

**Page rows:** the row builder `FUN_08098D40` shows a sequence's page 0 from `0x080EEF28` (`49 46 88 6B`, MIDI Out) or, with `0xB7` = 2, `0x080EEEE0` (`49 46 88 7A`); layer pages 1–4 from `0x080EEE98` (`85 86`). Lists are zero-terminated halfwords in 0x48-byte slots. The Mix line adds Rec Len (`0x19D`) to both page-0 lists; ours follow it, so page 0 has nine rows. The page model takes up to 47 rows (`FUN_080A5038`), and the same scrolling list shows the Clip pad pages' much longer lists.

**Layer 0:** `App_SetParam` / `App_GetParam` keep ids `0x6B`–`0x8A` in layer 0 by a bit mask (literal words `0x080A00CC` / `0x080996AC`, `0x20008001`: `0x6B`, `0x7A`, `0x88`); bit 9 adds `0x74`. Ids above `0xC0` go through the Mix line's `seqrec_set_layer` / `seqrec_get_layer` (`seqrec.S`), which with `WARP_MPE` also keep `0x1F0`–`0x1F2` in layer 0.

**The second UART:** `FUN_08044A0E` sends DIN to two UART queues, `mgr+0x630` and `mgr+0x2648`, always both. Stock's MIDI-thru path (`FUN_080449CC(mgr, msg, port)`) can pick one: 0 both, 1 `+0x630`, 2 `+0x2648`. Which jack each one is has not been traced, so DIN here keeps stock's "both". A per-UART option is possible later: Port values 3 and 4 would send to one UART only.

### Answers to the plan's open questions

- **A player's index from its pointer:** from its key (`player+0x18`, `0x1RRCC`), as row × 4 + column. The player array is engine `+0x127D0` (count `+0x128D0`), but the key is enough and survives graph rebuilds.
- **Is `Player_NoteOn` the only seq note path?** No. There are six senders (table above), and `Player_NoteOn` is only the live one. `FUN_08061E60` is the key-`0x400` node's. So the channel is chosen at the one send in pcmStreamer, not at the senders.
- **Layer-bag params and layer changes:** they reach the player's switch with their layer index (above). The patch keeps all four layers' values per player and follows `0xC0`. A layer queued by stock (`+0xC24`) takes over at its boundary, while ours applies at once. Notes still playing from the old layer end on their own channels.
- **Rows:** first planned on the layer pages (six of up to 47); moved to the sequence's page 0 (nine rows) after the first look, one setting per sequence.
- **The second UART:** see above. It is kept together with the first.

### Ids

`0x1E5` is not free: `App_SetParam` / `App_GetParam` treat it as a UI flag (`app+0x8CC7`). The Song DJ row's widgets use `0x1D0`–`0x1E4`. The registry has 500 slots. The new params are **`0x1F0`–`0x1F2`**, and `0x1E6`–`0x1EF` and `0x1F3` stay free.

## How it is built

The design changed from the plan: there are no hooks in `Player_NoteOn` / `Off` or `FUN_0805D56C`. Those are three of six senders, and a seventh path would be missed silently. Instead, every message carries what the send needs to know.

**The tag.** `player+0x68` = 64 + index × 12 + Port × 4 + mode. Each of the six senders copies it into the message at the moment it posts, so a queued note-off carries the Port and mode it was played under. Stock's own values there are 0 (nothing sets it) and 1–4 (the `0x400` node), both below 64. So an untagged message is stock's, byte for byte.

| Site | Kind | Stock | Does |
|---|---|---|---|
| `0x0808CDA2` | bl | `bl Param_RegisterInt` (`midiport`) | `mpe_register`: `0x74` as an enum, the three new params, and the state, which starts empty at boot. |
| `0x0809441C` | bl | `bl Param_Add` (case `0x47`, `0x88`) | `mpe_tpl`: stock's add, then ours. |
| `0x0805EBCE` | b.w | `ldrh.w r2, [sp, #0x24]` | `mpe_param_thunk` → `mpe_param(player, router, msg)`: keeps the sequence's values (posts for layers 1–3, which a layer push also sends, are dropped) and sets the tag. On a Port or mode change it first runs stock's all-off (`FUN_0805D56C(player, 0, router, 1)` and `0x0805E0F8(player, router)`), as stock's own `0x74` case does. It takes `0x74` over from stock, which would store the raw value. Players other than the 16 sequences go to stock. |
| `0x0804D004` | bl | `bl FUN_08044A0E` | `mpe_out_thunk` → `mpe_out(mgr, packet, din, usb, msg)`: masks DIN or USB by the tag's Port; in MPE picks the channel (and sends the setup and a stolen note's off first). A note-off in the table goes to its channel; one that isn't, in MPE, is dropped, because its note was stolen. |
| `0x080EEF32`, `0x080EEEEA` | bytes | `0000…` | page 0 rows `74 1F0 1F1 1F2`, after Rec Len. |
| `0x080A00CC`, `0x080996AC` | word | `0x20008001` | `0x20008201`: `0x74` in layer 0. |

**Tasks:**
- `cfg` and `gen` are written only by the audio task (the param switch).
- The channel table and `sent` are written only by pcmStreamer (the send).
- `gen` / `sent` replace a dirty flag: a change during a send is not lost.

**Memory:** `MpState` at SRAM4 **`0x38007150`–`0x380073E0`** (656 bytes), between RbVoice + diag (to `0x38007148`) and the Song DJ row's `DjState` (`0x38007400`). It holds a magic word, then per player 40 bytes:
- the note on each of 16 channels;
- the LRU order of the 16 channels (nibbles);
- the busy mask;
- `cfg` (Port 2 bits, mode 2, Last Ch − 1 4, bend 7);
- `gen`, `sent`.

The plan's `0x38004930`–`0x38005000` is not free on this line: it is Clip Deck's `DkState`. `mpe.c` asserts the range at build time.

**Flash:** about 1.7 KB in bank 1. Bank 1 has 5.2 KB left after the lineup cave.

## Bench

`tools/bench/run_tests.py --image tools/stretch/bungee/build-lineup/BLACKBOX_emu.BIN`, the `Seq MIDI out:` cases:
- **sites:** every hook decodes to its symbol, the page 0 rows, the layer-0 masks, the layer pages untouched, and letter U.
- **registry:**
  - titles, xml names, labels and ranges;
  - `0x73` / `0x75` untouched, and `0x1E6`, `0x1EF`, `0x1F3` free;
  - boot state: defaults, channels in order.
- **templates:** the real `Param_GroupTemplate` adds ours once, after `0x88` in `0x47`, and none to a layer bag (`0x48`) or a pad.
- **param switch:** the real player event loop (`FUN_0805E58C`) on a fake router, with 0x39 messages:
  - any param tags the player;
  - MPE / Port change the tag with stock's all-off first;
  - Last Ch / PB Rng bump the setup and nothing else;
  - posts for layers 1–3 are dropped; a layer switch changes nothing of ours, and stock's still runs;
  - stock `0x6B` still works;
  - the `0x303` player stays stock.
- **send:**
  - untagged and Off messages are stock's bytes;
  - Port DIN / USB, and USB with USB Dev Out off;
  - the Lower setup bytes;
  - LRU order, offs on their own channels, stealing the oldest, a stolen note's later off dropped;
  - an off after switching to Off still ends on its channel;
  - the same pitch twice;
  - Upper from MIDI Out 15 to Last Ch 13 on DIN, Multi from 9 to 4, Multi on all 16, Lower with only its master (stock bytes).
- **stock senders:**
  - the real `FUN_0805CEF4` / `FUN_0805D070` / `FUN_0805D56C` post the tag;
  - through the send, four notes on channels 2–4 steal correctly, and all-off ends each on its own channel.
- **preset attributes:** stock's XML parser and `<params>` reader (`FUN_08095910`) load `midiport` / `mpemode` / `mpelast` / `mpebend`; a preset without them gets Both / Off / 16 / 48.

Not on the bench:
- **The save side.** It writes every bag param by its registry xml name, the same names the load test reads.
- **The full `Pcm_PollMidi`**, which also renders audio.

## To try on the unit

1. **Rows:** a sequence's own page: Port / MPE / Last Ch / PB Rng under Rec Len; scroll and edit; the values stay when you switch layers.
2. **Off is stock:** MIDI Out on channel 1, MPE Off, Port Both. Notes on DIN and USB, as 3.1.T.
3. **Port:**
   - DIN only;
   - USB only (with USB Dev Out on; nothing with it off).
   - Check both DIN jacks / UARTs if the unit has two outputs.
4. **Lower:** with an MPE synth or MIDI Monitor on USB, MIDI Out 1, Last Ch 5.
   - The first note sends MCM + RPNs.
   - A 4-note chord lands on channels 2–5.
   - A 5th note cuts the oldest, and no notes hang after stop.
5. **Upper:** MIDI Out 15, Last Ch 13: channels 13–15, MCM N=3 on 16.
6. **Multi** 1 to 16: 16 notes on 16 channels.
7. **Change while playing:** switch MPE or Port with notes held (they end), change Last Ch / PB Rng / MIDI Out (the next note re-sends the setup).
8. **Save and reload** a preset with these set. Load a 3.1.T preset (all defaults).
