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

## Configuration transactions

`mt_config_begin` copies one immutable snapshot of 1-64 words (at most 256 bytes).
It refuses another request while a packet/transaction is pending; UI handoff must
retry later, never spin in the audio task. Four full chunks is the maximum, followed
by COMMIT. Revision zero is invalid; revisions increase within an instance and
cannot wrap without reset. The sender's `applied[]` revision snapshot changes only
on COMMIT ACK, and should supply the revisions passed to `mt_prepare`. Notes stay
queued behind the transfer. Reset or timeout aborts the unfinished transaction.
If COMMIT might have applied but its ACK is missing, an unrelated track reset
also resets the transaction's owner; notes cannot resume with an uncertain revision.

Packet kinds 3/4 are CONFIG_CHUNK/CONFIG_COMMIT, with count and word 4 zero.
Words 8-15 contain track, generation, revision (also header word 5), chunk index,
total byte length, schema, whole-image checksum, and payload word count. Words
16-31 hold at most 16 data words, with zero padding. COMMIT has no data and its
index equals the number of chunks. The image checksum uses the same wrapping
sums over all configuration words, with no skipped word. Schema identifiers are
opaque to transport; zero is invalid.

`src/dsp/config.s` stages chunks in a separate fixed 64-word buffer, checks order
and consistent metadata, and validates the complete image checksum on COMMIT.
`_mi_config_publish` must validate the actual schema, track, generation and revision
before atomically publishing into consumer-owned storage. It must not retain the
staging pointer. A missing or rejecting publisher leaves applied settings untouched
and produces no commit ACK. The V2 packet consumer must call this helper only after
packet/order/ownership validation, and call `_mi_config_abort` on priority reset.
Those integration callbacks are not implemented yet. Tests use a publisher fixture
to verify that only a complete valid commit changes its bank; they also execute
actual CF-generated chunk/commit packets on the SHARC staging code.

These components are built but deliberately **not hooked into the live stream**.
The V2 consumer/schema publisher, reply framing/heartbeat, and composed early-block
hook remain to be implemented. The current module still
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
