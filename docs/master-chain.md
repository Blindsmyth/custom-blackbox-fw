# Master chain on the Blackbox (3.1.T lineup)

The FX row becomes `DLY | RVB | AUX | EQ | CLR | CMP | MAX`. The master path becomes:

EQ (5-band dynamic) -> CLR (Airwindows Mastering2) -> CMP (Airwindows PurestSquish, or the stock
compressor) -> MAX (one-knob look-ahead maximizer) -> [Song DJ: loop capture, loop playback, DJ
filters and FX] -> a safety limiter. The DJ loop is recorded from the mastered project and played
back round the chain, so it is never mastered twice. With the chain on the safety limiter is MAX's
look-ahead limiter with no drive (`run_safety` in `fw/mc_b2.c`, 1.5 ms more latency): the stock
block limiter model hard-clipped the loop + project's overs for several ms before its gain arrived,
which clicked on the unit (2026-11-16). With the chain off it is the stock limiter model.

The DSP is the desktop prototype's plain C (`tools/master_chain/src/dsp`). It is checked against the
original Airwindows code (`tools/master_chain/reference`) and costs ~14% of a 256-frame block on
the M7 model (`tools/master_chain/m7`). The plugin in `tools/master_chain` is the UI reference.

## Where things go

**DSP: bank 2.** Bank 1 has ~6.9 KB free. The DSP is ~25 KB.
- **Linking:** it links into the existing bank-2 image (`.b2text`; state in `.b2bss`, about 3 KB
  of B2RAM). Bank 1 reaches it through a third `RbExt` word, `master` (`tools/stretch/rubber/rubber.h`),
  set in `lanes.cpp`.
- **Missing bank 2:** without a paired, ready bank 2 every hook falls back to the stock path.
- **Build:** the sources are added to `B2_SOURCES` (`build_patch.py`), so a DSP change changes the
  pairing id.

**Audio hooks** (`firmware/patches/3.1.9-song-dj/mixer.c`, `master.S`):
- **Chain on:**
  - `master_comp` (0x08053CA0) returns without running the stock compressor.
  - `mx_master` replaces `bl FUN_0804D880` at 0x0805353A.
  - That call copies bus 0 to the recorder's resample input, SE+0x15864.
  - `mx_master` runs CLR+CMP -> the copy -> MAX -> `mx_block` (capture, loop, DJ FX) -> the safety limiter on bus 0, and
    sets `done` so `mx_bus_tap` skips.
- **Resampling:** it records the mastered signal. With the compressor on, stock already records
  post-compressor.
- **Chain off:** no master processing, the stock limiter model as a safety (`MC_OP_LIMIT`).
- **EQ:** `mx_eq` runs the 5-band EQ instead of the stock EQ node (node at SE+0x28D8C, 4 bands,
  process 0x08047600), then the node's next stage as now.
- **`--mx-safe`:** the new site joins `MX_TAPS`, so `--mx-safe` drops it.

**Parameters.** Ids 0x13C-0x17B were never registered in stock and no patch uses them.
- **Avoid:** 0x13C, 0x14B, 0x152-0x154, 0x159 and 0x16F, which collide with UI event codes.
- **Chain, CLR, CMP, MAX:** 0x13D-0x14A (14). CMP Stock reuses the compressor ids of `master.S`.
- **EQ:** in the remaining runs. Band on/off is the stock "None" type.
- **Global vs per-preset values:**
  - Global values live in the global bag (`settings.tml`). It has 52 rows today against a capacity
    of 96, so the EQ needs the template's alloc at 0x0809481C raised to 128.
  - Per-preset values live in FX slot bag 3 (`App+0x3008`, key 0x330, saved in `preset.xml`).
  - A global toggle picks the source. The audio side reads a snapshot.

**UI**
- **FX row:** 7 buttons of 46 px. The Return page owns only 4 widgets, so 3 are created in `DjDeck`.
- **CLR / CMP / MAX:** the stock 8-knob grid of the Return page (screen 0x23), two sides of four.
- **CMP / MAX GR meters:** custom widgets with a max-hold that BACK resets.
- **EQ page (0x36):**
  - **Graph:** our draw takes over its graph vtable slot 0x080F0660 and draws the curve as the meter,
    straight into the 8-bit canvas.
  - **Five bands:** the hard-wired 4 is patched in `0x080AA9B0`, `FUN_080A9EFC`, the dot table and
    `eqtouch.S`.
  - **INFO:** swaps the four encoders to Thresh / Range / Attack / Release.

## Status (2026-11-03)

**Milestones 1 and 2 are built.** Bench: all `Master chain` cases pass, 1956 passed overall. The
4 that fail also fail on the committed build: Song vt 0x30 / 0x34, Lineup Fill / Grain.
- **Cost:** bank 2 takes ~183k cycles per 128-frame block, 14.3%.
- **Defaults:** the chain starts **off** (`Chain:` 0x13D in `settings.tml`). Switch it on on MAX.
- **Chain off** (bank 2 running): no master processing. No EQ, no CLR/CMP/MAX, no stock compressor
  (whatever its switch says; the render is its flag's only reader, so skipping it is the same as off).
  The Song DJ block, then the stock limiter model as a safety (`MC_OP_LIMIT`).
  Without the compressor in front the bus runs over 0 dBFS: the loop ring needs its 18 dB of headroom,
  or the loop is squared off and sounds crushed (found on the unit, 2026-11-11/12).
- **Bank 2 not running:** everything is stock.
- **Tools page 4:** the Master page (the stock compressor's settings) has no button any more. CMP's
  Stock type edits the same ids. The limiter's attack/release (0x1A8/0x1A9) are not reachable on the
  unit for now.
- **Knob sides:** CLR/CMP keep the side (left / right four) over the rebuild a knob turn causes. A
  page switch starts on the left.
- **AUX:** it does nothing yet.
- **Milestone 3 built (2026-11-11):**
  - **Meters:** CMP and MAX show a gain-reduction bar with a white max-hold marker and a
    "GR x MAX y" text above the knobs. They are drawn by bank 2 (`fw/mc_ui.c`, `RbExt.ui`).
    BACK on those pages clears the holds; the holds are tracked on the audio side.
  - **Values: Global / Preset** (0x14C, on MAX):
    - The live values are always the global bag's; each preset's session bag carries them too
      (`mc_session_tpl`, 0x08094500, seeded from the global set).
    - Global -> Preset keeps the global set aside (store ids 0x15A-0x16C) and makes the preset's
      values live; Preset -> Global brings the global set back. The page is rebuilt after a switch.
    - With Preset, a load makes the preset's values live (`mc_preset_load` from `mx_bpm_send`: stock's
      finalize loop passes on only BPM / Swing / 0xC2, so it is the load's hook). Only knob edits go
      back into the session, and they mark the preset edited (App+0x8CC4) so Save lights up.
  - **Copy at 0x0805353A:** voices read it back (0x08062784), so it is taken where stock takes it:
    after CLR + CMP, before the DJ block and MAX.
  - **Loop ring:** 18 dB of headroom (`RING_HEAD`), the bus before MAX runs over 0 dBFS.
- **Not on the unit yet:** milestones 4-5.

## Milestones (each one builds, passes the bench, and can go on the unit)

1. **Audio:** DSP in bank 2, `mx_master` hook, the chain parameters (global), bench cases
   `t_mx_master_*`.
2. **UI:** FX row of 7, CLR / CMP / MAX knob pages, chain on/off on MAX.
3. **Meters and storage:** GR meters with max-hold + BACK; the Global / Preset toggle with
   FX slot 3.
4. **Dynamic EQ:** the DSP in the EQ slot, 5 bands on the EQ page, INFO for the dynamics.
5. **EQ graph:** the graph drawn as the meter (range zone, live fill, max outline).
