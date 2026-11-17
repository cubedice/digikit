# CANOPY trig locks

Branch `codex/short-loop-proof`, starting at `5c35b33d`. Stage 1 (sequencer
trigs) and stage 2 (keyboard live recording) were committed before this work.
The packed stage 3 work is committed on this branch; see
[finding 147](../findings/147-canopy-trig-locks.md) for format and evidence.

The old six-MOD-offset experiment in `src-lock.c`/`.h` is no longer included
in the module. The active implementation stores absolute values in five
tagged stock lanes and filters them when the audio ISR applies a due event,
not when the sequencer prepares events ahead of time. It carries each snapshot
with the note through retries, validates the DSP sideband, and overlays values
before native modulation. A real panel/emulator test passed with the capture
diagnostic enabled (`out/canopy-packed-v3-proof.log`); no device was flashed.

The normal-enabled build initially lost locks when the held trig was released.
The stock editor marks the gesture at `0x4005ab18` before editing; CANOPY now
calls that routine for held edits and clears. The corrected image passed panel
recording, playback, clear, project save/reload, pattern copy/switch, a running
pattern transition, live recording, and separate locks on two tracks
(`out/canopy-packed-multitrack-proof.log`).

Focused host and DSP checks passed, including native modulation on a lock and
the sender/receiver transfer boundaries. The affected `mise run test` gate
failed with missing DNII firmware and Rust-for-ColdFire, pre-existing
repository-wide layer/format and fixture failures, and generated-object checks
run while the objects were being updated (`out/canopy-lock-full-gate.log`).
Lock-only steps and physical hardware qualification remain open.
