# CANOPY: USER exit, directional spread and pad/arp phase

Branch: `codex/short-loop-proof`. DTII OS 1.17 only. Findings are [D]/[O],
not independently audited [V]. The existing native TUNE, source LEV,
dedicated track LEVEL and held-SRC controls remain in place.

## User requests

- Allow leaving a learned USER chord using the chord control.
- Keep arp octave **spread**, in one direction at a time, from -3 to +3.
  Zero is the played octave; the pad is not transposed.
- Investigate uneven individual PLAY notes from either input source.

## Implementation

USER restricts the learned voicing only. Both directions of CHRD remain
editable, without erasing the learned shape. Signed spread uses up to sixteen
bounded candidates and omits out-of-range notes. CPST v2 migrates v1 upward
octave counts without changing their musical range; CPS8 carries the new
meaning through both configuration and presentation banks.

Same-pitch arp retriggers now start in phase with their held pad owner.
Independent oscillator resets could partially cancel at intermediate MIX,
depending on pitch and onset timing. Phase matching checks owner identity and
actual tuned coefficients. A per-call snapshot accounts for the pad rendering
before the arp; a deferred start uses the end phase instead. The instance
remains 800 bytes. Octave extensions, separate envelopes, independent drift,
gain and the linear mixer law are retained.

`dkscreen` now reassembles fragmented RtMidi SysEx messages. `devprobe` accepts
explicit MIDI notes with velocity/channel and sends note-off in `finally`.
Transient-hit segmentation is not an appropriate sustained-level metric;
use event-aligned windows when analyzing these recordings.

## Evidence

- SRC/preset/movement host suites: 100 passed. Keyboard/native suites: 54 passed.
- Initial spread-only compiled acceptance: passed, 660 seconds.
- Phase regression failed on the old DSP in eight direct/protected cases,
  then passed after the correction. Full-tail-pool and octave isolation added
  six passing cases. The focused spread/phase/instruction-bound run passed 46.
- The final affected CANOPY V2 DSP suite passed all 312 cases in 350 seconds.
- Screen tool: nine passing tests and physical ordinary RtMidi screenshots.
- Changed Python lint and whitespace checks pass.
- Isolated voice-kernel equal-velocity MIDI 48..84: Velvet 0.0160 dB and Reed
  0.0236 dB sustained RMS range. This excludes stock filtering/AMP/effects.
- Physical pre-update: confirmed PLAY but external MIDI initially OFF, so
  the earliest USB captures were stock-path hits and are excluded. Corrected
  captures use velocity 100, MIDI 60..71, and 0.35..0.95-second note windows.
  With the existing filter/AMP and effects sends off, pad-only varied 1.056 dB;
  MIX 76/127 varied 5.166 dB non-monotonically. No capture overflows.
- Originals were restored: source level 57, track level 85, ARP MIX 76,
  effects sends delay/reverb/chorus 43/52/19 and external MIDI OFF. Native
  encoder counts are not absolute values; screen readback matters. Discard
  the intermediate unverified dry captures.

The final full DSP and compiled acceptance logs are
`out/canopy-arp-phase-dsp-full.log` and `out/canopy-spread-phase-runtime.log`.
The final compiled acceptance passed in 585 seconds, including USER exit,
directional spread, native controls, preset migration, panel releases, tuning
and independent source/track level audio checks. TypeScript typechecking also
passed (`out/canopy-spread-types.log`).
The first final-package UI run timed out on the new negative-spread encoder
reversal; its ColdFire requested value still showed +3 and guards were clear.
The test now waits in emulated time around the affected encoder inputs and
acknowledges each small spread step instead of combining wall-time waits with
a large reversal. A run with long fixed emulated waits for every menu key was
stopped because it unnecessarily slowed unrelated navigation.

## Artifacts and preservation

- Firmware: `out/patch/canopy-spread.syx`, SHA256
  `8b4984b91d4bb347f56117f65c2abd18eea9b82617e347649be98d7becad20fb`.
  Preflight passed; runtime image ID `366a87b86ab545d2`.
- Module: `out/patch/canopy-spread-phase.dkm`, whole-file SHA256
  `d11148d37930cc2042b17c60e5cf6f2f2bce1dd306ecffdb588a06b253e66a3b`.
  Embedded DKM digest:
  `0cfdedf46d73fcd9cf7d1d539a9db570a1c6d259e637d8dd15f2f99c4d417597`.
- Before-update installed image: `6c03036ecf861957`. All three installed
  packages and inventory are backed up in `out/canopy-spread-install-backup/`.
  Overloop and remote packages/selection are to be retained.
- No settings record existed before preservation. The current sound was saved
  into empty CANOPY preset slot 4 and its v1 bytes were read back. Controls:
  `(10,0,49,23,140,76,4,3,1,25,0,0,0,0,5,15)`, Velvet, PLAY, local 120 BPM.
  Loading it after the update should preserve the sound and show spread 0.

## Physical installation [D]

The DTII accepted and rebooted into image `366a87b86ab545d2`. Readback matched
the new CANOPY package hash above and the unchanged Overloop/remote hashes.
All three loaded modules reported zero faults with guards enabled; safe,
crash-found, upload-open and card-read-only flags were false.

Preset slot 4 was loaded. Screens confirmed PLAY, MIX 76 and octave spread 0;
the original v1 saved record remains byte-identical. Source level is 57 and
track level 85. The user explicitly requested leaving external MIDI enabled.
EXTERNAL MIDI, USB and DIN are now ON on channel 1, and SAVE ROUTING completed.
Readback of `canopy/routes/v1` confirms enabled/USB/DIN/channel bytes 1/1/1/1.
Screens and the saved settings record are in `out/canopy-spread-hardware/`;
installation inventory is `out/canopy-spread-hardware-installed.json`.

Physical encoder checks also passed: SOUND B changed USER to MIN7, back to
USER, then MAJ7. ARP C displayed -3, +3 and 0. Screens are
`user-exit-left.png`, `user-return.png`, `user-exit-right.png` and
`arp-spread-{negative,positive,zero}.png` in that hardware directory.

On November 8, the installed image/package and healthy module inventory were
rechecked. Preset 4 was restored again after the control tests, returning to
PLAY, MIX 76 and spread 0. Original source level 57 and delay/reverb/chorus
sends 43/52/19 were restored. The saved routing record was rechecked as ON
for external MIDI, USB and DIN on channel 1.

## Physical level result and remaining issue [D/O]

Post-update dry captures are `out/canopy-play-dry-mixed-after/` and
`out/canopy-play-dry-pad-after/`, with zero overflows. Both use MIDI 60..71,
velocity 100 and the same 0.35..0.95-second windows. The mixed-minus-pad
level difference varies by 0.170 dB across the notes, compared with 4.236 dB
before the update. This supports the specific same-pitch cancellation fix.

It does **not** establish equal whole-device note levels. Post-update mixed
RMS spans 6.332 dB and pad-only spans 6.361 dB, both rising smoothly with pitch.
The earlier pad-only range was 1.056 dB, so the whole-device before/after
comparison is not a clean gain qualification despite matching visible filter,
AMP, modulation, source-level and saved CANOPY controls. FFT peaks confirm the
expected pitches in all four captures. The remaining common response must be
isolated from the stock processing/carrier state before claiming the user's
overall loudness issue solved. No compensation curve was added on this evidence.
Analysis is saved in `out/canopy-spread-hardware/audio-comparison.log`.

## Limits

The affected merge gate was previously attempted and blocked by missing
cargo-nextest, the wasm target, and the /tmp free-space guard. Focused passing
checks do not replace that gate. The wider stock pixel/modifier matrix and
independent image-byte audit also remain separate requirements.
