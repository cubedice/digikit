# CANOPY Stage 2 keyboard modes

User requested completion of Stage 2 after the Stage 1 checkpoint `2fc76879`.
Branch: `codex/short-loop-proof`. No device firmware or selection was changed.
Do not advance into Stage 3 without new direction.

## Implementation

The optional single-instance `core/instrument-play` adapter owns the bounded
musical engine in `src/cf/keyboard.c`. Physical FIFO ownership remains in
`core/midi-events`. Its optional mapper accepts at most eight notes or one
controller, retains transformed packets across retries, and retires the source
count only after the exact ACK. Silent selector batches retire locally; UI-only
diffs retire no physical events. Mapping failure quarantines its track. A
separate confirmed keys-up recovery advances the MIDI epoch without weakening
DSP boot trust. Absent consumers retain the original transport path.

PLAY has four admitted foreground notes and uses the existing two DSP release
tails. ROOT + TYPE has twelve silent selector types, last-held-root fallback,
same-channel octave or separate selector channel, and close/open/two inversions.
LATCH replaces the released phrase on the next press. LEARN auditions without
latch and accumulates four unique pitches. SAVE checks the displayed session and
revision; CANCEL/NO, source interruption and faults abandon capture safely.
Common generated tones retain audible tokens. Range failure rejects the whole
voicing. Normal DSP release remains one second; PANIC/reset uses its fast ramp.

Eight templates occupy one validated 48-byte `canopy/shapes/v1` settings record.
RAM save is immediate, card I/O occurs only on the UI task outside the IRQ mask,
and failed writes preserve dirty state for explicit retry. Other keyboard setup
choices are intentionally nonpersistent. Named SRC displays/presets and optional
automatic voice leading remain later work; there is no pad/arp claim in Stage 2.

## Validation and artifacts

- 29 TypeScript host scenarios compile and exercise the production C engine,
  transport and UI callbacks, including card failures/reload, stale SAVE, source
  interruption, capacity quarantine, stale releases, selector mappings and ACKs.
- 25 existing MIDI C/Rust host tests pass, including bound-session recovery and
  refusal to recover an invalid DSP session.
- Five instruction tests pass: admitted SHARC playback plus actual ColdFire
  ownership/dispatch and the new CF engine probe. The CF probe ceiling is 100000
  instructions per eight-event call and 100 for idle; observed 37333 and 48.
  Setup costs 22130. These are instruction counts, not hardware timing claims.
- Required changed-file checks pass host tests (232, 16 skipped), lint, types
  and layers. The format failure was fixed and rechecked. Global pathcheck has
  the pre-existing 256 stale references in 78 files; all changed docs pass.
  The focused suite now has 29 passing cases after a final direct-call output
  bounds test. Updated types, lint, formatting and build-current checks pass.
- Combined functional acceptance passes in 472.04 s (539.63 s including setup):
  normal DKM admission/selection, Stage 1 stereo/pedal/velocity, silent selectors
  and common-tone continuity, four-note learn/save, clean durable settings record
  readback, transposition, READY-gated panic/stale releases, NO cancellation and
  callback-fault retirement with responsive USB. Log:
  `out/canopy-stage2-final-live.log`.

Evidence versions: that full flow ran image
`73503bf704df964541474389a2347d147d4fc313ca51b64c5015ba3451d0e89c`.
The final bounds-only hardening was verified separately by the 29-case host suite
and the actual CF instruction probe (`out/canopy-stage2-bounds.log`), then rebuilt
and checked current. The image below includes that guard; its full menu/audio
flow was not redundantly replayed after this local defensive change.

`out/patch/canopy-stage2.syx` is patched from DTII OS 1.17 with
`core/instrument-play,core/digikit-menu`; its SHA-256 is
`dfe952d73db4e8527a190082ef4c2b1d4e0610bd5ec347aadf689292c6011230`.
The CANOPY DKM/DSP is unchanged from Stage 1. The existing dedicated
`canopy-stage1` Vitest project now runs combined Stage 1/2 acceptance in
`tests/regression/runtime/canopy-stage1.test.ts`, using the Stage 2 firmware.
It uploads/selects the real DKM, tests final stereo audio and real menu actions,
and ends with callback-fault retirement and a responsive upload service.

Logs use the `out/canopy-stage2-*` prefix. Firmware, PCM, OLED captures and other
derived artifacts remain ignored. Commit the rebuilt tracked core object JSONs.
Stage 6 still owns hardware timing, coexistence and release approval.
