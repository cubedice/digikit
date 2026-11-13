# CANOPY recording fixes

Branch `codex/short-loop-proof`. Three reports on the installed four-page build
(DTII image `1d4953c4df3f3193`): a held step blanks CANOPY's SRC page, live
recording gives no sound or one note instead of the chord, step recording
enters single notes. Evidence and layouts are in
[finding 125](../findings/125-canopy-recording.md).

Fixed and verified in the emulator (`out/patch/canopy-chord-rec.syx` with the
unchanged `out/patch/canopy-four-page.dkm`):

- Held step: sequencer byte `+0x268` (held-trig edit mark) no longer revokes
  CANOPY's page; the held step's locks are drawn and edited by CANOPY.
- Live recording: recorded presses also play on CANOPY's panel path (all held
  keys), with the stock audition muted on the frame path; chords are stored as
  chord-note locks (packed IDs 53..55) once the stock recorder commits the
  step, and play back as chords.
- `canopy-record.test.ts` (extended) and `canopy-lock.test.ts` (knob moved to
  SOUND B, COLOR in the four-page layout) pass; host suites pass except the
  seven pre-existing `canopy-preset.test.ts` failures that also fail on the
  previous commit (they still assume the retired SRC layout).

Open:

- Step recording (RECORD + STOP, notes entered with KEYBOARD held): entries
  bypass the note action, so the first install did not cover them. The key
  wrapper now detects each entry from the step's note list, sounds it and
  stores the held chord (finding 125). Emulator-verified (commit `0efa0ead`):
  a held D, F, A sounds together, is stored and plays back as the chord.
  Not yet installed on the DTII (it runs `ab2cd5ce545be167`, commit
  `37010d88`); flashing needs the user's go-ahead.
- Physical hardware: installed on the DTII on 2026-11-13 at the user's request
  (image `1d4953c4df3f3193` -> `ab2cd5ce545be167`; report
  `out/canopy-chord-rec-hardware-installed.json`). Current sound kept in preset
  4 and restored; settings, routing and module health unchanged. After the
  flash the device hung on "Success! Rebooting" (the old image's soft reset did
  not fire; finding 75 lists the real-device reset as open); a power cycle
  booted the new image. Listening tests on hardware are the user's.
- KEYBOARD mode is off after entering grid recording; users must re-enable it
  to play notes there.
