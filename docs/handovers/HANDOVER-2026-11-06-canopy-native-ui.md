# CANOPY native interface implementation — 2026-11-06

Implements the accepted [native interface proposal](../design/canopy-ui-review.md)
on `codex/short-loop-proof`, based on `d6ed8f9752a01b395c033dd06ff3849cd58b64c2`.
Firmware-derived images and packages remain ignored under `out/`.

## Changes

- SOUND A and H retain the actual OS1.17 TUNE and Sample Level handlers,
  formatters and drawing. The dedicated LEVEL/DATA resolver now returns native
  Track Level code 10 on both SOUND and ARP. Custom overlays preserve those
  cells, the left track column, header and page dots.
- Custom cells use native fonts, native encoder templates and the firmware's
  existing show-values/focus state. The old full-page pixel painter is removed.
  ARP H aliases SOUND G's Arp Mix parameter; no preset IDs or gain stage changed.
- CANOPY Settings exposes a refreshing STATUS page with chord, complete note
  names, latch/pedal state and arp/clock state. The old KEYBOARD shortcut remains;
  its external-enable toggle is now explicitly labelled EXTERNAL MIDI.
- The built-in KEYBOARD view forwards resolved native pitches/velocities and
  physical key releases into a separate PANEL ownership route. It does not
  require external MIDI to be enabled. PANEL supports PLAY/ROOT/LEARN through
  the existing policy and cannot alias USB/DIN sustain or pitch bend.
- Leaving KEYBOARD or selecting another track cancels PANEL ownership. The
  main controller retains KeyboardView on exit, so the implementation wraps
  its native active-state toggle as well as checking view update/detach.

## Artifacts

- Firmware: `out/patch/canopy-native-ui.syx`, SHA256
  `6a534f4da64ef833ce55a047fb919c88b819fdcbf12e9a755f3c0603819e7ab0`.
- CANOPY module: `out/patch/canopy-native-ui.dkm`, whole-file SHA256
  `7b1c67996991e22c5e5934ff8b3e06ad6a39d84c12b1d0f791c893fd31b3698f`.
  Its embedded DKM digest (the packer's reported hash) is
  `018a80b602efc3346942aad3cc72438d504c9bd46a92fc9a611e9ab2ee58313b`.
- The firmware includes the CANOPY/Overloop core adapters and runtime 0.5.4.
  The isolated UI/audio acceptance selects only CANOPY. Firmware preflight
  passes; the module uses the rebased DKDS 8 instrument format.
- This revision has not been installed on the physical DTII. Read-only INFO
  still reported runtime 0.5.2 / image `f064c83d13ad47d4`; its older measurements
  do not qualify this image. A read-only screenshot request timed out.

## Validation

- 82 UI/keyboard/status host cases; 25 MIDI ownership/transport cases;
  84 native menu/UI cases passed.
- 271 complete CANOPY DSP/instrument component cases passed, one skipped.
  Eight additional cases cover PANEL arp bend isolation and native retuning.
- The final keyboard-exit and encoder-template revision passed all ten native
  bridge host tests. Regression TypeScript and changed Python lint pass.
- The initial default-value firmware frame had zero differing pixels against
  the stock Oneshot oracle in TUNE, source LEV and the track-column regions.
  That observation is not the full fractional/end-range/focus/held-values matrix.
- Compiled runtime acceptance passed on the preceding `b73e4f7d...` image in
  `out/canopy-native-ui-runtime-b73-pass.log`. It covers local-key chord/release/exit,
  the native pages and held values, controls, modal isolation, presets, held-note
  tuning, and independent final-output source/track level sweeps.
- That run's stereo output RMS was 0.0140711 at full track level, exactly 0 at
  zero track level, 0.0141124 after restoring track level on ARP, and
  0.00000000400 at zero source level. Track-level edits preserved the source
  Sound slot. Results: `out/canopy-native-levels-b73.json`.
- Pixel comparison at matching 0.00 TUNE / 100 source LEV found zero differing
  pixels in both complete protected cells in normal and held-SRC states.
  Track values differed in those captures, so that pair excludes the left
  fader. Retained comparison frames are from the `b73e4f7d...` run; the SRC
  painter is unchanged in the final image. Results:
  `out/design/canopy-native-ui/pixel-comparison.json`.
- The latest image fixes Status's narrow-column title and drains telemetry at
  the UI tick cadence independently of repaint. Long mode/chord pairs wrap to
  keep their complete text. Its 44 keyboard/settings host cases and preflight
  pass. The final compiled run also passed (604 seconds):
  `out/canopy-native-ui-runtime-final.log`. Status freshness, three admitted
  notes and modal isolation passed. Its actual Status and final SOUND frames
  are retained in `out/design/canopy-native-ui/`.
- Final-image RMS: full track 0.0140263; zero track 0; full track restored on
  ARP 0.0140960; zero source 0.00000000587. Results:
  `out/canopy-native-levels.json`. These are emulator final-stereo measurements,
  not physical USB/main/headphone measurements.

The required affected merge gate was attempted and did not pass: this environment
lacks cargo-nextest and the wasm target, and host lifecycle checks hit the /tmp
50 GB free-space guard (approximately 7 GB available). These are also recorded in
the preceding rebase handover. The broad run was stopped; its log is
`out/canopy-native-ui-merge-gate.log`. Focused passing tests do not replace that gate.

## Remaining acceptance

The complete stock pixel/modifier matrix and physical post-fader audio/keyboard checks remain
separate acceptance requirements. No claim of full hardware or exact-state-matrix
qualification is made by this handover. Findings and firmware anchors are marked
[D], not [V], pending independent image-byte review.
