# 3.1.U: Seq MIDI out, MPE, Port and program change

Part of the lineup image (`../3.1.9-mix-bungee/build_patch.py --lineup`, `lineup/BLACKBOX.BIN`). The menu reads `3.1.U`: 3.1.T plus this. Design, stock code and tests: [docs/mpe-out.md](../../../docs/mpe-out.md).

Seq screen → the sequence's own page (MIDI Out, Rec Len) has four more rows, one setting per sequence:

- **Port:** Both / DIN / USB. Where the sequence's notes go. Both is stock; USB still needs Tools → USB Dev Out on.
- **MPE:** Off / Lower / Upper / Multi.
- **Last Ch:** the last note channel, 1–16 (default 16). The first note channel is the sequence's own **MIDI Out** channel, as in Ableton's MPE / Multi-channel settings.
  - Note channels are first … last, either way round, without the zone's master.
  - **Lower:** master 1. MIDI Out 1 (or 2), Last Ch 16 → notes on 2–16.
  - **Upper:** master 16. MIDI Out 1, Last Ch 16 → notes on 1–15; MIDI Out 15, Last Ch 13 → 13–15.
  - **Multi:** no master. MIDI Out 1, Last Ch 16 → all 16.
- **PB Rng:** member pitch bend range, 0–96 semitones (default 48).

How it behaves:
- **MIDI Out must be on**; its channel is the first note channel.
- **Channels:** a note takes the member channel free longest. With all busy, the oldest note is cut.
- **Note-offs** go to the note's own channel.
- **Setup:** the MPE Configuration Message and the bend ranges go out before the first note after any change or preset load.
- **Changing Port or MPE** ends held notes first, as stock does for the MIDI Out channel.
- **Presets:** saved as `midiport`, `mpemode`, `mpelast`, `mpebend`. Older presets get the defaults.

Files:
- `mpe.c`: registration, templates, the per-sequence state and tag (audio task), and the channel choice at the send (pcmStreamer).
- `mpe.S`: the three stubs.
- `sites.py`: the hook sites with their stock bytes, the page rows and the layer-0 id mask.

Layer pages A–D have **Bank:**, **Sub:** and **Pgm:** (`---` or 1–128), as on an Ableton MIDI clip. A layer's set goes out when the sequence starts and when the layer takes over, before its first note, once until a stop or another layer: CC 0, CC 32, Program change, on MIDI Out's channel (the MPE master in Lower / Upper). Saved as `pcbank`, `pcsub`, `pcpgm`.

State: SRAM4 `0x38007148`–`0x380073FC`.

Not yet tried on the unit. The checklist is at the end of docs/mpe-out.md.
