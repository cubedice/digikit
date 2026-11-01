# CANOPY Stage 3 pad engine

Requested after Stage 2 commit `ee3b8986`, on `codex/short-loop-proof`.
Stage 3 is complete at its functional emulator boundary. No physical device,
installed firmware or selection was changed.

## Implementation

Four foreground groups plus two shared tails retain Stage 1/2 ownership. Each
uses one normalized rotating fundamental, with analytic second/third harmonics.
Velvet has a rounded odd spectrum; Reed adds even/odd harmonics. Cosine-based
weights fade each harmonic before Nyquist at the current bent/drifted pitch.
There are no discontinuous waveform generators or unbounded spectral loops.

The six-word CPS3 configuration is version, family, color, drift, attack and
release. The adapter validates/copies it atomically and allows updates while
active; startup still waits for the old reset tail. UI changes coalesce while
configuration ACKs are pending. Applied revisions remain nonzero MIDI admission
proof; they are no longer fixed to one. CPS1 remains for legacy sine probes.

The final five KEYBOARD rows expose those controls. Defaults: Velvet, color 64,
drift 0, attack 5 (80 ms), release 15 (1000 ms). Attack index maps to 10+14*v ms;
release to 40+64*v ms from full amplitude. These setup settings do not persist or
acquire destination-pattern values yet; compatible switches keep current values.
Stage 5 owns SRC pages and presets.

Family/color use a 20-ms smoother and per-sample coefficient interpolation. Drift
is a deterministic 6.667-second triangle seeded by instance generation/track,
with opposite signs for alternating tokens. It rotates temporary coefficients,
never accumulating into base MIDI bend tuning; maximum excursion is five cents
and smaller at high frequencies. REPLACE preserves modulation state as well as
phase for the <=64-sample emergency tail. CREATE/RECOVER/UNLOAD clear all state.

State grew from 528 to 544 bytes. Shared tuning/envelope tables total 2560 bytes.
The fixed 0.08 gain bounds six full-velocity groups below 0.75 without note-count
normalization. A final 32-sample scan rejects NaN, infinity and magnitude above
one, clears the block and returns a fault before the stock chain. Policy still
admits only one track; no silicon CPU or Overloop coexistence claim is made.

## Validation

- Initial combined component run: 76 passing cases, including the actual CF
  engine, SHARC ownership/transport/carrier and long pitch/release tests.
- Final V2 run: 58 passing direct/protected cases, including CPS3 endpoints,
  interleaved-instance modulation, invalid-config atomicity, guard canaries,
  preserved reset-tail spectrum and NaN/infinity/over-level silence.
- Declared limits are 100000 process and 1000 reset instructions. Observed Stage 3
  protected maxima: 20830 process, 423 reset. These are instruction counts, not
  hardware cycles. Logs: `out/canopy-stage3-components.log` and
  `out/canopy-stage3-final-v2.log`.
- Spectrum comparisons use actual kernel output at MIDI 0,60,100,110,119,127 for
  both families. High-note checks would detect folded second/third harmonics.
  Other cases cover maximum positive/negative drift, six-group headroom, queued
  replacement and a one-second fundamental hold/release at pitch extremes.
- Required changed-file preflight passes 233 host tests (16 skipped), lint, types
  and layers. Formatting was fixed and rechecked. Global pathcheck retains the
  known 256 stale references in 78 files; changed docs have zero stale references.
- Drift wraparound passes two additional direct/protected cases; note ownership
  persists and phase stays bounded (`out/canopy-stage3-drift-wrap.log`).
- Full keyboard regression passes in 482.93 s (554.75 s total): stereo continuity,
  independent releases, pedal, velocity, silent selectors, common tones, learned
  shape/card save/transposition, panic/tombstones, NO cancellation and callback
  fault retirement with responsive USB. Log: `out/canopy-stage3-keyboard-live.log`.
- Stage 3 sound acceptance passes in 581.25 s (651.08 s total): live family,
  color, drift and release changes retain the same voice token; both output
  channels carry the added Reed harmonic; stock filter closes/reopens and reverb
  remains audible after the module buffer becomes exactly zero. Log:
  `out/canopy-stage3-live.log`; metrics: `out/canopy-stage3-audio.json`.
  Dry/closed/open RMS: 0.027996 / 0.006159 / 0.027922. Reverb-tail RMS: 0.010511.
- The initial rapid encoder sweep stopped at 74 rather than 127. The final test
  driver waits for applied sound values, uses representative color/drift values,
  and bounds stock filter sweeps by their measured final-audio effect.

## Artifacts

`out/patch/canopy-stage3.syx`: 1995936 bytes, full-file SHA-256
`8257f777cd6f2df9305bf8664ade78f8f7ec1e3c0d9287710622260d5577e57b`.

`out/patch/canopy-dt2.dkm`: 10804 bytes, full-file SHA-256
`7ae4c401d9de8138dde08d501caa4ecd214ba6ee8c58e0463ced93913e772afe`.

The pack command's printed digest is its payload digest, not the full-file hash
above. The two live runs used the pre-output-guard DKM (10660 bytes, full SHA-256
`c0e7924c335a94d04cca4b734b9abe9ae1b5fc59ca453f6b0909b8af512f050a`).
The final 32-sample output guard changes no valid audio; it was added while those
runs were in progress, verified separately in the final 58-case V2 run, rebuilt,
packed and checked current. The full live flows were not redundantly replayed
for that guard-only change. The firmware SHA above is shared by both runs.

Stage 4 is arp/strum. Stage 5 is preset/named SRC UI. Stage 6 remains hardware,
CPU, multi-instance and Overloop coexistence qualification. Do not expand those
stages just to replace remaining Stage 3 acceptance work.
