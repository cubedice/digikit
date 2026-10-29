# Instrument MIDI ownership

Work in progress for the [CANOPY implementation](../../../../docs/design/canopy.md), DTII OS 1.17 only. This is the ingress foundation, not a playable instrument or an installable CANOPY release. No route activates without an `instrument_midi_ready` provider confirming the instance's epoch/generation. That provider is not implemented yet, so including this adapter preserves stock routing.

The runtime's existing USB hook composes an optional `mi_channel_message` callback ahead of upload handling. This adapter adds the separate hash-pinned DIN router hook. Both receive complete messages, normalize USB sources 2/4 and DIN sources 8/16, and leave SysEx, clock, unsupported controllers and unclaimed channels on their stock paths. Firmware receive-channel enable settings still apply before these hooks.

The fixed ownership ledger has 32 port/channel routes, 128 physical press records and a 128-event ring. Repeated pitches pair FIFO by token. Releases follow the original track/generation after rerouting; reset, panic and ignored sequencer-mode presses retain tombstones until their physical release. A capacity failure resets all configured instrument instances and quarantines their routes until explicit recovery with keys released. It does not claim unrelated stock routes.

Events remain in the ring until an exact epoch/sequence ACK. Instance resets use reserved priority bits outside the ring; ordinary dequeue waits for reset ACK. Epoch and generation checks reject stale acknowledgements. Token exhaustion requires explicit recovery into a new stream epoch. All mutation is serialized by the CF adapter's interrupt mask; a future audio sender must use the same critical-section contract. The readiness callback must be bounded, allocation-free and nonblocking.

Checks:

```sh
cargo test -p dkmidi-host
uv run python -m tools.patchkit.patchkit build core/midi-events core/runtime
DIGIKIT_CF_SCRIPT=out/native/target/nogen/release/examples/cf_script \
  uv run python -m pytest tests/patchkit/test_midi_events.py -q
```

The host tests execute the production C implementation, including the runtime dispatcher. The ColdFire component test compiles the same cases for the target and executes them on its instruction core. Neither establishes real USB/DIN latency, stock AMP lifetime, DSP transport coexistence or final audio. Those remain required gates before enabling routes in a release.
