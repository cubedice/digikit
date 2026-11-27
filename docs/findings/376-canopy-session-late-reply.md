# 376. CANOPY's instrument session lost to a late pre-session reply

Why did CANOPY's machine sometimes not come up on the DTII, on some boots and not others? The module loaded, but its V2 instrument never
published. DT2 OS 1.17, the hardware set with `digikit/debug` selected, 2026-11-27. Marks as in `docs/FINDINGS.md`.

## What it looks like [V]

- CANOPY's module record stays in MODULES and LOADS says ACTIVE with result OK, but CAPACITY shows `INSTRUMENT_ROWS 0/8`, CANOPY has no
  machine type and no DSP range. On some boots the record is gone from MODULES altogether.
- Neither is an unload: `RELOAD_STATE.op` is 0 (no live job since the boot) and `dk/fault` is not written.
- The V2 loader's status words (`DKRT_INSTRUMENT_FILE`: phase, record, result, cause) tell the endings apart: phase 3 live, phase 6 the
  record freed (the module vanishes), phase 9 failed and kept until the next boot.
- An install check that compares module names does not see it. CANOPY is up when CAPACITY shows an instrument row (or CANOPY's MODULES
  record has type 16) some 25 s after the boot.

## The failure, captured [V]

Four soft restarts, read with PEEK. Three were live at 16 to 25 s. One failed at about 7 s:

- `DKRT_INSTRUMENT_FILE` [9, 0, `E_DSP_XFER`, 100], `DKRT_INSTRUMENT_SESSION` [5, 1, 1]: the link owner reported `Blocked(Failure::Boot)`,
  so the instrument session was blocked before CANOPY's load was admitted.
- In `core/midi-events` (its code runs from `services/instrument.dks`; the statics' offsets are in `instrument.addrmap.json`):
  - `reset_seen` 0: the DSP did not reset;
  - `registry_guard`: ready 1, invalid 1, last accepted heartbeat `0x436c` at CF block 9112;
  - `mt_guard_fault`: reason 4, block 9113, reply heartbeat `0x436a`, magic `0x494ea101` (`MT_REPLY_MAGIC`).

Reason 4 in `mt_reply_guard_tick` is a valid pre-session reply after the guard is ready. This one arrived one block after the last
accepted reply, with a heartbeat two older. The guard's failure is sticky: `mi_registry_reply` returns -2, the coordinator's
`observe_boot` reports `Failure::Boot`, and CANOPY stays blocked until the next boot.

## Why the frame is late, not a downgrade [V]

- `runtime/platform/midi-events/src/dsp/reply.s` builds one reply per DSP block, its heartbeat one more each time. The magic is `0x494ea101`
  until the DSP's session word is set and `0x494ea102` or `0x494ea103` after. A heartbeat names one frame, and every pre-session frame
  is older than the bind ACK.
- The two reply rows alternate (the status phase in `reply.s` is chosen so that a reader of every second row still sees ACK frames), and
  the ColdFire reads the current row once per block. A row read just after the bind ACK can still hold an older frame.
- `mt_reply_reordered` forgave late session replies only. The late pre-session frame fell through to reason 4. Whether one lands in that
  window is timing, which is why boots differ.

## The fix [V]

The guard keeps the bind ACK's heartbeat (`MtReplyGuard.bound`). `mt_reply_reordered` drops a pre-session reply 1 to 8 heartbeats older
than it (`MT_REPLY_REORDER_LAG`) as late, so the session stays bound. A pre-session reply at or after the bind ACK's heartbeat still
faults (a downgrade), and so does one with the heartbeat of an accepted session reply (a conflicting copy). The host case in
`mi_test_session_guard` replays the device's record and fails without the fix.

## Ruled out, and open

- A DSP reset: `reset_seen` was 0. **[V]**
- The machines service parking CANOPY's remembered machine number 16: a V2 instrument owns no machine row until it publishes, so
  `park_absent` tries to park 16 as an absent module's number (`MACHINE_TYPE_EVICTED`, why 3). It appeared on good and bad boots alike,
  and the DSP did not take the write on any observed boot. Whether a park that is taken can hurt the load is **[O]**.
- `tests/patchkit/test_midi_events.py::test_ownership_and_dispatch_on_coldfire`, which runs the transport cases under ColdFire, fails to
  link (`instrument_track_keyboard_routed` unresolved) when `cf_script` is built; without it the test is skipped. The cases were run
  natively. **[O]**
