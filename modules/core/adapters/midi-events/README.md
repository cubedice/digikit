# Instrument MIDI ownership

Work in progress for the [CANOPY implementation](../../../../docs/design/canopy.md), DTII OS 1.17 only. This is the ingress foundation, not a playable instrument or an installable CANOPY release. No route activates without an `instrument_midi_ready` provider confirming the instance's epoch/generation. That provider is not implemented yet, so including this adapter preserves stock routing.

The runtime's existing USB hook composes an optional `mi_channel_message` callback ahead of upload handling. This adapter adds the separate hash-pinned DIN router hook. Both receive complete messages, normalize USB sources 2/4 and DIN sources 8/16, and leave SysEx, clock, unsupported controllers and unclaimed channels on their stock paths. Firmware receive-channel enable settings still apply before these hooks.

The fixed ownership ledger has 32 port/channel routes, 128 physical press records and a 128-event ring. Repeated pitches pair FIFO by token. Releases follow the original track/generation after rerouting; reset, panic and ignored sequencer-mode presses retain tombstones until their physical release. A capacity failure resets all configured instrument instances and quarantines their routes until explicit recovery with keys released. It does not claim unrelated stock routes.

Events remain in the ring until an exact epoch/sequence ACK. Instance resets use reserved priority bits outside the ring; ordinary dequeue waits for reset ACK. Epoch and generation checks reject stale acknowledgements. Token exhaustion requires explicit recovery into a new stream epoch. All mutation is serialized by the CF adapter's interrupt mask; a future audio sender must use the same critical-section contract. The readiness callback must be bounded, allocation-free and nonblocking.

## Ordered transport components

`src/cf/transport.c` retains a 32-word packet until an exact epoch/message ACK.
Each ordinary batch contains at most eight consecutive events for one track and
generation, tagged with its committed configuration revision. Revision zero holds
notes in the ring. Retries are byte-identical even if the requested revision
changes. A reset supersedes unacknowledged notes without releasing physical-key
tombstones. After 900 blocks without ACK, the sender disables routes and services
priority resets; fresh sounding input requires explicit stream recovery. Message
identity never wraps within an epoch. The caller must serialize these operations
with the ownership ledger; these functions are not yet called by the frame ISR.

Internal wire version 1 (`0x494e0101`) uses eight header words: magic/version,
kind/count, epoch, message sequence, first event sequence, configuration revision,
first-send block, and checksum. Word 6 is **not** an ingress timestamp or latency
measurement. Kinds 1/2 are EVENTS/RESET. Event records follow the design's
token/generation/packed-argument layout. RESET has count zero, zero words 4/5,
track/generation in words 8/9 and zero padding. The checksum is wrapping uint32
Fletcher sums `a += word; b += a; a XOR b` over all words except word 7, including
padding. This is an internal prototype contract, not a released SDK ABI.

`src/dsp/packet.s` validates the complete packet before publishing anything.
`receiver.s` orders accepted messages, rejects gaps and stale epochs, suppresses
replays, and allows priority reset to jump a gap. A new epoch must begin with
RESET. The optional `_mi_apply_packet` consumer must validate generation and
configuration, apply the entire packet atomically, and return 1 only afterward;
only then does the receiver advance its ACK. On a newer epoch, reset must clear
all old instances. Missing or rejecting consumers produce no ACK.

These components are built but deliberately **not hooked into the live stream**.
The V2 consumer, configuration chunk/commit transaction, reply framing/heartbeat,
and composed early-block hook remain to be implemented. The current module still
does not claim keyboard routes. Target tests feed actual ColdFire-produced reset
and eight-note packets to the SHARC validator, and exercise receiver retries,
gaps, reset preemption, consumer rejection and stale epochs. They do not establish
live SPI delivery or the independent-note audio lifetime gate.

Checks:

```sh
cargo test -p dkmidi-host
uv run python -m tools.patchkit.patchkit build core/midi-events core/runtime
DIGIKIT_CF_SCRIPT=out/native/target/nogen/release/examples/cf_script \
  uv run python -m pytest tests/patchkit/test_midi_events.py -q
```

The host tests execute the production C implementation, including the runtime dispatcher. The ColdFire component test compiles the same cases for the target and executes them on its instruction core. Neither establishes real USB/DIN latency, stock AMP lifetime, DSP transport coexistence or final audio. Those remain required gates before enabling routes in a release.
