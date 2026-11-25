# Personal features (not for a release)

Features for one setup. They are in every `--lineup` build; **`build_patch.py --lineup --release`** leaves them all out (no code, no Tools rows, no params). Each one is under `WITH_PERSONAL`.

## Tap Out

**Tools → Clock → Tap Out:** under BPM. Off, or a note value from the Delay's own list (same labels):

> 1/64, 1/32, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/2, 1 bar

- **Sends:** CC 15 = 127 on channel 1, **DIN only**, once per note value, locked to the Blackbox clock.
- **1/64:** 1.5 clock ticks, so its taps are alternately 2 and 1 ticks apart (counted in half ticks).
- **Changing the value while playing:** the next tap comes one new note value after the last tap.
- **Phase:** Start puts the next tick on a tap, so taps sit on the beat.
- **Saved:** globally, `settings.tml` `tapout`; the default is Off.
- **Change it:** channel, CC and value are `TAP_CH` / `TAP_CC` / `TAP_VAL` in `personal.h`.

How it works:
- The engine posts a clock tick (message `0x56`, 24 PPQ), Start `0x57` and Stop `0x58` to the MIDI queue. The audio task's `Pcm_PollMidi` pops them and sends F8 / FA / FC.
- Seq MIDI out's hook at `0x0804D018` (`mpe_msg`) sees every popped message. `personal_msg` counts the ticks there and sends the CC with `FUN_08044A0E(mgr, packet, DIN 1, USB 0)`, in the task that owns the MIDI queues.
- Tools → Clock's MIDI clock send setting only picks where F8 goes (`FUN_0804D504` sets `pcm+0xA68C` / `+0xA68E`). The ticks pass through either way.

**To check on the unit:**
- whether the engine posts ticks while the transport is stopped (then taps run while stopped);
- whether it posts them with MIDI clock send off (expected, but not traced to the poster).

Files:
- `tapout.c`: the param (the Delay's labels, param `0x33`), the template, the tick counter.
- `personal.h`: id `0x1D9`, the CC settings, state in backup SRAM `0x38800638`.

The hooks are `mpe.c` `mpe_msg` and `mixer.c` `mx_register` / `mx_global_tpl` (all under `WITH_PERSONAL`), plus the Tools → Clock list word `0x080EEDFA` (`build_patch.py`).
