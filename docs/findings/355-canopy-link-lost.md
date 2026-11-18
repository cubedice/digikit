# 355. CANOPY "LINK LOST" on hardware: a faulted note transport that nothing recovered

On a Digitakt II with the image of 2026-11-17 (CANOPY, Overloop, PRISM and remote) CANOPY showed LINK LOST and stopped playing
notes until a restart. This finding records what the device showed, why the state lasted, the two changes made and what is
still not known. DT2 1.17; marks as in `docs/FINDINGS.md`.

## What the device showed [D]

Read over USB with the remote module while the device was in that state (2026-11-17):

- DIGIKIT > CANOPY STATUS: `ROOT LINK LOST`, `LINK LOST`, `TRACK 11`.
- DIGIKIT > CANOPY > INPUT, the status row: `RECOVER OR RESTART`. That row is chosen by `mi_transport.faults` (not by a
  quarantined key) in `control.c`: the note transport of core/midi-events had faulted.
- HEALTH: CPU 36 %, DSP 65 %, DSP MAX 70 %. No crash record; all four modules loaded with no faults.
- RECOVER KEYBOARD (the menu action, `mi_keyboard_recover`) succeeded: the row then read `READY`. That action refuses when the
  reply guard is invalid, so the guard was valid: the fault came from the transport's own rules, not from the sticky guard.

## Why it lasted

`fault()` in `midi-events/src/cf/transport.c` sets `faults`; after that `mt_prepare_mapped` sends nothing, a configuration is
refused (`mt_config_begin`), the status queue is emptied every block (`adapter.c`), and every track is reset. Only a new epoch
clears it (`mt_init`), and the epoch advanced only in `mi_keyboard_recover` (the menu) and at the session bind. So one fault
silenced the instrument until the owner found the menu action or restarted. The same state also explains settings that seem
not to apply: with `healthy` at 0 or a configuration that cannot be sent, a mode or preset change never reaches the tracks.

The transport faults when a sent packet is not acknowledged within 900 blocks (0.6 s), when the reply heartbeat stands still
for 900 blocks, or when it runs backwards.

## A way to lose every acknowledgement [O]

The DSP builds one reply per block (`midi-events/src/dsp/reply.s`). Ordinary frames carry the acknowledgement; a status frame
(for the SRC page and the status menu) carries none. Status frames were sent on every odd heartbeat while the status mailbox
was ready. The two reply records of core/dsp-link alternate, one per block. A reader that sees only every second heartbeat,
with odd parity, would get status frames alone and no acknowledgement, and the transport would fault 0.6 s after the next
packet. Nothing here shows that the ColdFire ever reads only one of the two records on hardware; the emulator sees every
block. It is a mechanism that fits a fault with a valid guard, not a demonstrated cause. The device had no debug module
loaded, so the transport's words (`mi_transport`, `mt_guard_fault`, `ip_control`) could not be read.

## Changes

- **Automatic recovery** (`instrument-play/src/cf/control.c`, `recover_tick`): after about a second of fault (45 UI ticks),
  unless the DSP reported a voice fault for this generation, the control code does what RECOVER KEYBOARD does
  (`mi_keyboard_recover`, then every instance released), at most eight times per boot. `ip_recoveries` counts them. Host test
  `tests/regression/host/canopy-link-recover.test.ts`: the wait, a refused recovery with an invalid guard, a recovery, the voice
  fault exception and the bound.
- **Status cadence** (`reply.s`): one eligible block in three carries the status (`_mi_status_phase`) instead of every odd
  block. A reader of every second block then gets both kinds. `tests/patchkit/test_midi_events.py` checks the cadence for both
  heartbeat parities.

## Open

- **[O]** The cause of the fault on the device. With the debug module loaded, the next occurrence can be read: `ip_recoveries`,
  `mi_transport` (faults, heartbeat, heartbeat block, pending), `mt_guard_fault`.
- **[O]** Whether the heavier DSP load of the always-on PRISM (finding 354) plays a part: the DSP read 65 to 70 %.
- **[O]** Recorded chords: a trig recorded with several keys carries chord-note locks and plays them in PLAY whatever the
  track's mode (`keyboard.c`, by design). Whether that is what "a mode change is not applied to trigs" means is to be asked.
