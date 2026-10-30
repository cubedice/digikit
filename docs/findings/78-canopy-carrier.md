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

### Configured envelope and transition policy follow-up

**[D]** The probe now calls the stock configuration routine at `0x1cba9e` before
rendering. It no longer seeds hold/release state or amplitude by hand. The
investigation uses mode 1, normalized attack 0 and HOLD 1, with the remaining
normalized arguments 0/1/0 and final flag 0. The routine produces HOLD index 127,
mode 1 and zero runtime state. A single first-flag pulse starts its envelope;
the second flag releases it. These are component call arguments, not resolved
CF MIDI fields.

The probe also executes the assembled adapter
[carrier policy](../../modules/core/adapters/midi-events/src/dsp/carrier.s),
feeding its emitted flags into the stock envelope. It supplies conflicting stock
trigger and release flags on every held block. All **nine** checks pass over
1,500 held blocks: the counter advances from 31 after onset to 48,031, with no
retrigger; QUIET emits release and a new ACTIVE edge wakes again. The report has
exactly two wake edges and one release edge. Input is still constant 0.25 and
the full mixer is not executed.

Eight [CANOPY adapter checks](../../tests/patchkit/test_canopy_carrier.py) also
pass. They exercise the policy with the real kernel's ACTIVE/QUIET result,
including held notes, independent releases, local tails, per-track isolation,
unowned stock pass-through and invalid indexes. The kernel and firmware-envelope
checks are separate component experiments, not a continuous live device chain.

### Still open

The stereo handoff now publishes a block-stamped pair once per track and reads
it without mutation for both voices. Ten adapter tests cover both endpoint
tracks, odd-first and repeated reads, stale blocks, an initial zero stamp,
ownership loss, per-voice stock flags and invalid indexes. The firmware probe
checks both reads on every block before feeding the pair into its envelope call.
This handoff is now used by a component-tested envelope entry hook.

**[D] Entry-hook follow-up:** `src/dsp/envelope.s` in the MIDI adapter replaces
the first instruction at `0x1cbb57`, executes its displaced 16-normal-word stack
reservation, and resumes at `0x1cbb5a`. It derives the voice by bounded lookup of
the 32 aligned 44-byte records relative to a configured base; unknown records
fall through. It substitutes only stack arguments +1/+2 when a current owned
pair exists. The audio pointers and count remain stock.

The firmware probe now overlays an entry jump in its private loaded-memory view.
It supplies conflicting stock flags at the original entry, so only the hook can
apply the published pair. Producer and firmware cores remain separate; the harness
copies the producer's actual publication into the firmware core each block.
This proves the entry interposition, not live frame delivery. No SYX is written.
Twenty-four adapter tests check the hook at both stereo endpoints, scalar/SIMD
register and status preservation, the displaced stack adjustment, argument
sentinels, and fallback on stale/missing/disabled/unaligned/out-of-range inputs.

**[D] Frame/render follow-up:** The early-block wrapper has an optional carrier
callback that increments the stamp and invalidates all old pairs, even on wrap.
The CANOPY raw render entry now publishes its ACTIVE result and configuration
readiness through the core carrier owner. This owner retains a previously active
track through reset tails and the release edge. The kernel keeps ACTIVE set for
the entire block containing final tail samples; reporting QUIET at that block's
end would release the downstream envelope before those samples were processed.
Connected instruction-core tests now run frame, render and envelope entries in
one memory image without copying flags or stamps. They supply the envelope-record
base and stub packet receive/reply; this is not running-firmware delivery.

Resolve the live workspace binding, normal envelope configuration and both flag writers before adding
the hook to the production manifest. Use idle-to-active to start the envelope once; avoid
rearming it for subsequent keys. Retain the carrier through local release tails
and pending onsets, then retire both stereo voices on QUIET. Prove neutral shared
velocity and downstream LEVEL, pan, mute, filter and FX in running firmware.
The [CANOPY spec](../design/canopy.md) still requires that final-output gate before
the pad engine and UI are enabled.
