# 357. CANOPY "LINK LOST" on a track switch: a toast for a status that is only outstanding

With CANOPY on two tracks of a Digitakt II (image `5f664d0d90c67436`, main 9f2fba5e), selecting the other CANOPY track
showed LINK LOST in the title bar every time. The link was not lost. This finding records what the device showed, the
cause and the change. DT2 1.17; marks as in `docs/FINDINGS.md`.

## What the device showed [D]

Read over USB with the remote module on 2026-11-17, CANOPY on tracks 11 and 12, the SRC page open:

- TRK + TRIG 12, then a screenshot every few hundred milliseconds. At 0.02 s the page of track 12 is drawn and the title
  bar shows the pattern name. At 0.27 s the title bar reads `LINK LOST`, and it still does at 3.0 s. The keyboard caption
  below reads `NO NOTES` the whole time, which the page draws only with a fresh status.
- TRK + TRIG 11: the same.
- No transport fault: the INPUT status row read `READY`, no module fault, no crash record.

## Cause

The DSP reports the status of one instance, the focused one (`_iv_focus` in `instrument-play/src/dsp/play.s`). The focus
follows the selected track: the next UI tick bumps the revision of the new instance, its bank goes out with the focus flag
(two chunks and a commit, each acknowledged), and only then do status frames of that instance arrive
(`instrument_play_tick`, `tick_instance`). `is_fresh` needs a fast status of the last 150 ms.

The SRC page is drawn as soon as the track is selected, before that hand-over. `cv_build_owner` found no fresh status, set
`fault = 1` and the heading `LINK LOST`, and `ip_src_draw` showed the heading as a toast (`src-native.c`). The firmware
keeps a toast for seconds, long after the status had arrived. With one CANOPY track the focus never moves, so this showed
only with two or more.

The same state follows a change of mode or generation (`is_select` clears the status) and a stall of the UI task longer
than 150 ms (`is_poll` discards what queued up meanwhile).

## Change

`status.c`: a status is *awaited* after its selection (`is_select`), after a focus change or a track select
(`is_await`, called from `instrument_play_tick` and `instrument_select_track`) and after a stall of the UI task or an
overflow of the status queue (`is_poll`). `is_waiting` is 1 until the first status arrives or 600 ms have passed
(`IS_WAIT_MS`, the stall limit of the note transport). While it is 1 the display has no fault, no heading and no
`LINK LOST` caption (`CvDisplay.waiting`). A status that had arrived and then stops under regular polls is a lost link
after 150 ms, as before. The flag uses the padding byte of `IsStatus` and `fast_at` (unused while no fast status is
valid), so the struct keeps its size and offsets.

`ip_src_fault_toasts` counts the fault headings the draw shows as a toast.

## Evidence

- **[D]** `modules/machines/canopy/tests/track-switch.device.test.ts` (DT2, emulator): CANOPY on tracks 11 and 12, six
  switches with the SRC page open. Without the change (the test and the counter alone): 6 fault toasts for 6 switches.
  With it: 0, the status of each selected track arrives, the note transport does not fault.
- **[D]** `tests/regression/host/canopy-src.test.ts`, scenario `status-wait`: the wait after a selection, its end after
  600 ms, an immediate loss when a running stream stops, a focus change, and a stall of the UI task.
- **[D]** On the device with the change (image `50bed6f2e07945b6`, 2026-11-18): four switches between tracks 11 and 12
  with the SRC page open, screenshots at 0, 0.25, 0.6, 1.5 and 3.0 s after each. None shows `LINK LOST`; the INPUT status
  row reads `READY`.

## Open

- **[D]** The owner describes what they saw after the install of 2026-11-17 as a message at the top that goes away by
  itself, which is this toast. The fault of finding 355 (a faulted note transport, `RECOVER OR RESTART` in the INPUT
  menu) is a different state; its cause is still not known.
- **[O]** The hand-over itself takes a few UI ticks, during which the keyboard caption is empty. Its length on the device
  was not measured.
