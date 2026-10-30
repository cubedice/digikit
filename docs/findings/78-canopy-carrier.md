# CANOPY carrier and stock envelope, DT2 1.17

2026-10-29. **[D]** Single-agent static inspection and bounded concrete-core
execution. No second-agent byte verification or running-device acceptance yet.

## Separate sample activation from AMP lifetime

The supplied DT2 OS 1.17 SYX has SHA-256
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`.
Its extracted section 7 has SHA-256
`0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2`.
The analysis catalogue labels this identical DSP blob as 1.16; these observations
were made on extraction from the supplied **1.17** image.

The sample-record arm routine at short-word PC `0x1c4eaf` sets ACTIVE and seed
pending, resulting in word `0x00010001` at record byte offset `0x1b8`. Its caller
in the voice dispatcher is at `0x1c6582`. It does not receive the independent
envelope record. In the component probe its execution leaves that record
unchanged. Calling this routine alone therefore does not establish AMP wakeup.

**[C] [D]** The older voice-hook section in
[finding 06](06-sharc-engine-and-startup.md) identifies `0x1cdd8b` as level/envelope
gain. Inspection of its complete body, including the branch to `0x1cddff`, instead
shows quantization: table-selected bit counts, power-of-two shifts, rounding and
integer-to-float reconstruction. Treat it as a bit-depth-reduction candidate,
not the carrier envelope hook. The older measured LEVEL/filter/reverb results
remain separate evidence; this correction concerns the function label.

The envelope candidate at `0x1cbb57`, called at `0x1c6f29`, operates on an
independent 11-word record. The dispatcher derives its record base from workspace
`+0xd604` and advances by `0x2c` per voice. It passes audio input/output buffers and
two stack flags followed by the sample count. The routine includes attack, hold
and release branches; entry flags can restart its envelope. Mapping those flags
back to live MIDI and CF frame writers remains open.

## Reproducible component experiment

[canopy_carrier_probe.py](../../tools/sharc_dev/canopy_carrier_probe.py) refuses
unrecognized firmware hashes and executes the original instructions through the
existing SHARC runner. Each call is limited to 10,000 instructions; the CLI permits
at most 1,500 blocks. Firmware bytes and generated dossiers stay under ignored
output directories.

```sh
uv run python -m tools.sharc_dev.canopy_carrier_probe --blocks 1500 --out out/canopy-carrier-analysis/probe.json
```

The probe seeds an explicit synthetic envelope record: HOLD index 127, mode word
1, state 3, internal amplitude -1.0, and constant input 0.25. This is **not** a
claim that normal carrier setup has been implemented. With both entry flags zero:

- 1,500 blocks / 48,000 samples pass unchanged.
- State remains 3 and the hold counter advances to 48,000, rather than resetting
  each block. Thus unchanged output is not the result of continual retriggering.
- Setting state 4 explicitly enters release. The maximum absolute output after
  the first sample is about 0.09319, below the 0.25 input.
- Raising the first entry flag for one block restarts the envelope. Its last
  sample returns to 0.25; the following block passes unchanged again.

All seven report checks passed. This establishes a sustained path in the actual
envelope instructions, not the full filter/mixer output, correct live flag
ownership, keyboard behavior or a device CPU measurement.

## Remaining carrier integration

Resolve the normal envelope configuration and both flag writers before adding
the V2-only wake/retire hook. Use idle-to-active to start the envelope once; avoid
rearming it for subsequent keys. Retain the carrier through local release tails
and pending onsets, then retire both stereo voices on QUIET. Prove neutral shared
velocity and downstream LEVEL, pan, mute, filter and FX in running firmware.
The [CANOPY spec](../design/canopy.md) still requires that final-output gate before
the pad engine and UI are enabled.
