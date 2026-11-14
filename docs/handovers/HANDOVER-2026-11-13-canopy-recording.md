# CANOPY recording fixes

Branch `codex/short-loop-proof`. Three reports on the installed four-page build
(DTII image `1d4953c4df3f3193`): a held step blanks CANOPY's SRC page, live
recording gives no sound or one note instead of the chord, step recording
enters single notes. Evidence and layouts are in
[finding 125](../findings/125-canopy-recording.md).

## Latest hardware installation

Installed the onset, FM envelope and held-step chord-preview update on the DTII
at the user's request. The firmware image changed from `ae201859f2e5246c` to
`bf49e4f08419374c`; the updated CANOPY package and the unchanged Overloop and
remote packages were read back and verified. All three modules reported zero
faults after reboot, and CANOPY's instrument row was admitted. The user's
current sound was preserved in the previously authorized preset 4 and
restored; settings and USB/DIN routing were verified unchanged. The backup and
installation report are in `out/canopy-onset-chord-install-backup/` and
`out/canopy-onset-chord-hardware-installed.json`. The new onset ramp has not
yet been auditioned on the physical device. [D]

## Rapid step-entry follow-up

The earlier step-record fix could miss notes when several keys were pressed
before the UI observed the first commit. The adapter now queues completed
stock note-list writes and pairs them with pending key presses. It retains
released keys' queue positions, ignores release and repeat events, and
discards pending writes when the recording mode or selected track changes.
The host regression covers these cases, including re-pressing a key before
its prior commit. The candidate uses `out/patch/canopy-step-record-final.syx`
and the existing `out/patch/canopy-four-page.dkm`; it has passed firmware
preflight. On that final image, `canopy-record.test.ts` passes: a rapid
D/F/A press sounds and plays back as a chord, with three started entries,
three matching commits, no queue drops and no expired waits. The focused
native suite (17 tests) and CANOPY patchkit integration suite (25 tests) pass.
The DTII still runs the earlier `e8c7cd7103116212` image; this follow-up has
not been installed or tested on hardware.

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
  Installed on the DTII at the user's request (image `ab2cd5ce545be167` ->
  `e8c7cd7103116212`; report `out/canopy-steprec-hardware-installed.json`);
  the device rebooted by itself this time.
- Physical hardware: installed on the DTII on 2026-11-13 at the user's request
  (image `1d4953c4df3f3193` -> `ab2cd5ce545be167`; report
  `out/canopy-chord-rec-hardware-installed.json`). Current sound kept in preset
  4 and restored; settings, routing and module health unchanged. After the
  flash the device hung on "Success! Rebooting" (the old image's soft reset did
  not fire; finding 75 lists the real-device reset as open); a power cycle
  booted the new image. Listening tests on hardware are the user's.
- KEYBOARD mode is off after entering grid recording; users must re-enable it
  to play notes there.
