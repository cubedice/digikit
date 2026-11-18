# 356. CANOPY settings across a power cycle: a working copy in the settings store

On a Digitakt II the CANOPY settings of a track did not come back after a power cycle. This finding says why, what was added
and what is not proven. DT2 1.17; marks as in `docs/FINDINGS.md`.

## Why they did not come back [D]

Read from the code and the earlier findings (36, 40, 121, 351), not measured on the device:

- CANOPY's per-track settings travel with the Elektron Sound as a 40-byte extension of the saved record (finding 351). The
  extension is written only when the stock Sound writer runs, which the fork sees at an explicit project save.
- A CANOPY edit changes the association store in RAM (`sa_put`) and no stock Sound byte, so nothing marks the project's
  mirror dirty. What the firmware reloads at boot therefore holds the extension of the last save, or none.
- `sa_follow` claims a Sound without an extension with the state the instance has at that moment. After a boot that is the
  default state, and the next save would write the defaults.
- The only tested path was "save the project, power cycle, load the project by hand"
  (`modules/machines/canopy/tests/persistence.device.test.ts`).

## The working copy

`instrument-play/src/cf/work-copy.h` and `control.c`:

- One extension per carrier track (16), in the settings key `canopy/work/v1` (660 bytes).
- Updated whenever a track's bound state changes: an edit that is published (`sa_publish`) and a state that is adopted at a
  bind (`sa_follow`). Written about two seconds after the last change (60 UI ticks), from the UI task.
- Applied to the first bind of a track after boot, in place of the extension of the boot records, and put into the
  association store, so a later project save carries it.
- The boot window closes about three seconds after the first bind of the boot, or after about 20 s of a healthy instrument
  without any bind. After that a track follows its Sound as before: a project loaded later brings its own settings.
- `ip_work_info`: boot copies applied, writes of the record, the last write's result.

## Evidence

- **[D]** `tests/regression/host/canopy-work-copy.test.ts`: the record round-trips; a wrong or truncated record is refused
  whole; a change makes the copy dirty and the same state does not; a boot copy exists only for stored tracks after a decode,
  stays until taken, and ends with the window.
- **[D]** The regression host suite and the device tests of CANOPY (arp, needs, persistence, copy-handoff, pattern-copy,
  sound-transfer) and PRISM pass with it.
- **[O]** The boot path itself is not proven in the emulator: it has no model of the power-off, and its cold boot does not
  bring back a project with CANOPY tracks, so no first bind of the boot happens there. The proof is on the device: edit a
  CANOPY control, wait a few seconds, power cycle, and read the control.

## Open

- **[O]** A project loaded by hand within the boot window (before any CANOPY track has bound, in the first 20 s) gets the
  working copy on its CANOPY tracks once. The copy carries no identity of the project.
- **[O]** Whether the firmware's own unsaved Sound edits survive a power-off on a device (finding 40 saw them not reach the
  arena in the emulator). If they do not, a CANOPY track comes back with its last CANOPY settings on the Sound of the last
  save.
- **[O]** Whether the boot Sound loads run before the association store can exist (`ip_sa_ensure` refuses with interrupts
  masked); `ip_sa_info` on the device after a boot would tell. The working copy does not depend on it.
