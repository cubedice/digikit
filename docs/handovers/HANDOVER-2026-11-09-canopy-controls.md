# CANOPY: direct performance controls and slower lists

Branch: `codex/short-loop-proof`. DTII OS 1.17. This follows the physically
installed wavetable build in `HANDOVER-2026-11-08-canopy-wavetable.md`.

The user reported that tiny knob turns changed TABL/SHAP too quickly, CHRD
remained stuck on AUTO, and keyboard mode and sound-source controls were split
between SRC and DigiKit setup. They confirmed that list selection should be
slower. The implementation consolidates these choices on the native SRC pages.

## Behavior

- SOUND B / CHRD selects PLAY followed by the twelve existing generated chord
  types, including USER. PLAY is editable and replaces the read-only AUTO label.
  Switching PLAY/root mode retires the old phrase and requires fresh keys;
  changing chord type within root mode updates the held root.
- TONE B / WAVE selects VELVT, REED, ANLG, GLASS, VOCAL, ORGAN or METAL. It
  updates family/table together and removes the need for a separate engine
  chooser. Legacy selections retain the last table and modulation settings.
  Only TONE C-G are inactive for the two legacy sounds; SOUND D remains COLR
  for those sounds and POS for wavetables.
- Discrete lists require four raw encoder ticks per choice. Partial motion is
  independent per knob, with no time-based acceleration. Reversals, context
  changes and changed imports discard partial motion. Endpoints clamp and
  discard excess movement. Numeric macros and native TUNE/LEV/LEVEL are unchanged.
- SETUP is now five short groups: INPUT, CHORD LEARN, SOUND SETUP, WAVETABLE
  TIMBRE and PRESETS. The global DigiKit shortcut is CANOPY. Controls already
  available on SRC are removed from setup. Input routing, learning, envelopes,
  clock/BPM, expression and explicit preset save/load remain available.

The UI uses existing logical IDs and serialized mode/chord/family/table fields;
it does not add an event source, a DSP mode, a preset version or native project
automation. Composite choices are never stored as expanded chord/table values.
Family and mode changes invalidate captured encoder tokens. Matching applied
revisions still own pending feedback. Existing native value rendering is retained.

Review also found a USER inconsistency: generated voicing was applied to learned
intervals despite the VOIC control being disabled and shown as learned. USER now
bypasses those transforms while retaining the generated-voicing preference for
the next standard chord. Three regressions demonstrated the failure before the
fix. The output-capacity fixture was updated to construct nine commands without
depending on that bug; its rejection and output-buffer checks remain intact.

## Validation and artifacts

- All 153 focused host cases pass across SRC/native, settings/preset and keyboard
  suites. The first run intentionally demonstrated the three USER failures;
  the final keyboard run passes all 49 cases. Logs:
  `out/canopy-usability-host-first.log`,
  `out/canopy-usability-keyboard-host.log`,
  `out/canopy-usability-keyboard-final.log`.
- Final regression TypeScript checks and changed-test formatting passed,
  including the strengthened held-note device assertions.
  `out/canopy-usability-typecheck-final.log` and
  `out/canopy-usability-format-final.log` record that pass.
- ColdFire object rebuilt with 8,262 decoded instructions. The DSP object is
  unchanged. Firmware patching and preflight passed; MAIN staged size is
  3,661,636 / 4,193,280 bytes. Logs: `out/canopy-usability-build.log`,
  `out/canopy-usability-patch.log`, `out/canopy-usability-preflight.log`.
- Candidate firmware: `out/patch/canopy-source-ui.syx`, 2,071,840 bytes,
  SHA256 `9f1b5b2ce4807ab53a05247f46701deba82a90076552c3ec6fbaa186ed4e0f6b`.
  It uses the unchanged `out/patch/canopy-wavetable.dkm`, SHA256
  `73bb61df9414806c09671d7bba4eec593647e7464f9632a10d46e50da5f3760c`.
- Independent source review found no actionable control/ownership defects.
  An artifact audit matched the CF object's source digest, all non-relocation
  bytes, all 1,183 relocations resolvable from plan symbols, and all in-place CF
  patch hashes. Outside instrument-play, changes are 13 import relocations,
  the image ID (`56edcafce3ba99d0`) and shifted record/tail addresses. Every
  non-MAIN section is byte-identical to the installed build, including the
  complete DSP section. The audit did not separately re-resolve 258 fixed-port
  and external-alias relocations; preflight validated the complete candidate.
  Complete compiled Stage 5 acceptance passed (one test, 2,297.56 s total) in
  `out/canopy-usability-stage5.log`. It covers held-note PLAY/root transitions,
  direct WAVE family changes with held gates/tokens, disabled legacy TONE
  controls, native tuning/levels, learning, USER exit in both directions and
  v3 preset recall. The final state reports zero DSP traps/missing blocks,
  no CF error, no stalls/underruns and no instrument guard fault. Evidence:
  `out/canopy-stage5-native-state.json` and
  `out/canopy-stage5-native-instance.json`. The Stage 6 fixture was adapted to
  the same direct controls but was not rerun; its DSP/coexistence code is unchanged.
- Preserved and visually reviewed the SOUND and TONE captures, including held
  values, in `out/canopy-usability-screens/`. Stock graphics remain while labels
  become values, as required by the existing native presentation contract.
  Final-output level checks passed: track-full RMS 0.01461466, track-zero 0,
  ARP-page track-full 0.01467605 and source-zero 5.736e-9. See
  `out/canopy-native-levels.json`. No physical audio was captured for this revision.
- Required changed-file checks completed against immutable base `0b2fdb74`, in
  `out/canopy-usability-changed.log`, and remain red. Lint/typecheck succeeded;
  formatting reports 16 untouched historical runtime fixtures, and Python
  formatting reports untouched existing files. The broader regression host
  run has 402 passes and 31 failures: 30 runtime-fixture cases are blocked by
  the existing free-space guard (7.2 GB available), and one remote-helper
  deadline assertion fails. The focused CANOPY files all pass in that run.
  Both failing host files subsequently pass all 62 cases with temporary files
  on the workspace volume (`TMPDIR=out/usability-test-tmp`), which has enough
  space; see `out/canopy-usability-gate-retry.log`. No production or test changes
  were needed for this retry. All 433 regression-host cases therefore pass
  across the original run and targeted retry. Formatting still prevents a
  passing changed-file gate; no merge-gate pass is claimed.

## Physical installation, 2026-11-09

At the user's explicit request, the candidate above was installed on the DTII.
The complete image was accepted and the device rebooted to `56edcafce3ba99d0`.
INFO reports no safe mode, crash, open upload or read-only card. CANOPY,
Overloop and remote loaded with zero module faults and guards enabled. All
three module files were read back and match their pre-installation hashes;
the existing CANOPY package required no replacement.

Fresh backups are in `out/canopy-controls-install-backup/`. The current Metal
sound was saved into previously empty preset slot 2 as a 96-byte v3 record.
Existing presets 3/4 and routing records remain byte-identical. The preserved
sound is PLAY, METAL, POS 3.05, DEP 14%, HZ 0.35, TRI and TRIG; native TUNE is
0.00, source LEV 87 and track LEVEL 112. The preserved preset was loaded again
after the temporary control checks. USB/DIN and external MIDI remain enabled
on channel 1, confirmed in both saved routing and the INPUT screen.

Physical knob checks show CHRD staying PLAY after one raw count and becoming
MAJ after the fourth; WAVE stays METAL after one count and becomes ORGAN after
the fourth. All three SRC pages and the compact five-group setup were captured.
A transient LINK LOST toast appeared after menu/reset transitions; two later
STATUS captures report PLAY NO NOTES, LATCH OFF and PEDAL UP, and the native
header was observed clear afterward. These observations establish installation,
settings preservation and UI behavior, not physical audio or timing qualification.
No physical audio was captured during this installation.

Logs and metadata: `out/canopy-controls-hardware-flash.log` and
`out/canopy-controls-hardware-installed.json`. Preserve this backup, the earlier
wavetable installation backup and the five pre-existing untracked native UI mockups.
