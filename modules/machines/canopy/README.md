# CANOPY implementation

This directory contains the stage-1 sine kernel for the
[CANOPY design](../../../docs/design/canopy.md). It is **not an installable module**:
there is no machine manifest, V2 descriptor, firmware carrier integration or UI yet.

`src/dsp/voice.s` implements four independent notes in a caller-owned 128-byte bank.
Each note has its own token, gate, oscillator phase, velocity and linear envelope.
Attack is fixed at 80 ms; release falls at full-scale per second (a note released
during attack ends sooner). Output is mono, in 32-sample blocks at 48 kHz. The sine
rotator normalizes its phase vector once per block to limit numerical drift.
`src/dsp/tuning.s` supplies MIDI 0–127 coefficients at A4=440 Hz.

Note-on rejects duplicate tokens, invalid notes/velocities and a full bank without
changing existing notes. Note-off matches the exact token and releases only that
note. Zero-velocity Note On must already have been normalized by MIDI ingress.
QUIET is returned only after every local release finishes, and the entire output
block is always written. Independent banks allow separate track state.

This is deliberately the lifetime-test kernel, not the finished allocator. It has
no voice stealing, de-click reset, sustain pedal, bend, chord modes, arp or preset
controls. These operations must not be advertised by a ready instrument yet.

Run its concrete SHARC instruction-core checks with:

```sh
uv run python -m pytest tests/patchkit/test_canopy_voice.py -q
```

The tests check independent phase/velocity/release, exact token handling, capacity,
memory guards, silent idle output and pitch/envelopes across sustained renders.
The five tests passed on the SHARC instruction core. Sustained checks cover one
second at MIDI notes 0, 69 and 127, then release to silence; waveform error against
the ideal sine/envelope stayed below 0.0002 in sample amplitude. This is a numerical
check, not a measured device CPU or latency claim.
They do not establish stock AMP lifetime, final track output, MIDI latency or
device performance. The current V1 adapter skips inactive stock voices and clears
configured state at stock note-on; CANOPY needs the separate V2 lifecycle and
carrier path described in the design before this kernel can receive live input.
