# Instrument MIDI ownership

Work in progress for the [CANOPY implementation](../../../../docs/design/canopy.md), DTII OS 1.17 only. This is the ingress foundation, not a playable instrument or an installable CANOPY release. No route activates without an `instrument_midi_ready` provider confirming the instance's epoch/generation. That provider is not implemented yet, so including this adapter preserves stock routing.

The runtime's existing USB hook composes an optional `mi_channel_message` callback ahead of upload handling. This adapter adds the separate hash-pinned DIN router hook. Both receive complete messages, normalize USB sources 2/4 and DIN sources 8/16, and leave SysEx, clock, unsupported controllers and unclaimed channels on their stock paths. Firmware receive-channel enable settings still apply before these hooks.

The fixed ownership ledger has 32 port/channel routes, 128 physical press records and a 128-event ring. Repeated pitches pair FIFO by token. Releases follow the original track/generation after rerouting; reset, panic and ignored sequencer-mode presses retain tombstones until their physical release. A capacity failure resets all configured instrument instances and quarantines their routes until explicit recovery with keys released. It does not claim unrelated stock routes.

Events remain in the ring until an exact epoch/sequence ACK. Instance resets use reserved priority bits outside the ring; ordinary dequeue waits for reset ACK. Epoch and generation checks reject stale acknowledgements. Token exhaustion requires explicit recovery into a new stream epoch. All mutation is serialized by the CF adapter's interrupt mask; the frame sender uses the same critical-section contract. The readiness callback must be bounded, allocation-free and nonblocking.

## Ordered transport components

`src/cf/transport.c` retains a 32-word packet until an exact epoch/message ACK.
Each ordinary batch contains at most eight consecutive events for one track and
generation, tagged with its committed configuration revision. Revision zero holds
notes in the ring. Retries are byte-identical even if the requested revision
changes. A reset supersedes unacknowledged notes without releasing physical-key
tombstones. After 900 blocks without ACK, the sender disables routes and services
priority resets; fresh sounding input requires explicit stream recovery. Message
identity never wraps within an epoch. The caller must serialize these operations
with the ownership ledger; the frame callback supplies that critical section.

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
RESET. `consumer.s` implements `_mi_apply_packet` with a 16-track generation and
committed-revision bank. Events must match both fields. Configuration must match
the generation and advance the revision. The complete packet is validated before
any callback, so a malformed later event cannot partially apply a batch.

The optional `_instrument_reset`, `_instrument_events` and `_instrument_configure`
callbacks return exactly 1 only after atomic application; rejection must have no
effects. Reset receives the packet in R4 and must clear all sounding instances on
a new stream epoch, or just the identified track within the current epoch. The
bridge clears ownership only after callback success and aborts pending configuration.
Events receive the complete packet in R4. Configuration receives copied staging
values in R4 and metadata in R8, and must validate its schema and copy values into
instrument-owned storage. No callback may retain staging pointers. The callbacks
obey the scratch-register contract documented in `src/dsp/consumer.s`.

These are internal lifecycle callbacks, not the public V2 module descriptor.
The instrument engine and callbacks are not implemented yet. Missing or rejecting
callbacks produce no reset/event/commit ACK and cannot enable keyboard routes.

Lifecycle component tests execute the production receiver, configuration staging
and consumer together on the SHARC instruction core. They cover reset before
configuration, rejected callback retries, stale generations/revisions/epochs,
duplicate suppression, malformed multi-event batches, cross-track reset isolation
and aborted configuration commits. The composed frame tests also include this
consumer under the existing SIMD and arithmetic-state preservation checks.

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
The lifecycle bridge now provides the ownership gate and publisher wrapper; the
instrument-specific schema callback is not implemented yet. Tests use a publisher fixture
to verify that only a complete valid commit changes its bank; they also execute
actual CF-generated chunk/commit packets on the SHARC staging code.

## Reply and heartbeat components

`src/dsp/reply.s` builds a bootstrap ACK-only reply once per requested tick:
magic/version `0x494ea101`, accepted stream epoch, accepted message sequence,
wrapping block heartbeat, eleven zero reserved words and a checksum. The checksum
uses wrapping Fletcher sums over words 0-14; magic is published last. This internal
format is separate from the final twelve-word CANOPY status payload, which will
require a new reply version. DSP-link copies `_instrument_reply` into its reserved
reply rows at the end of each block.

`mt_reply_read` reconstructs words from the reserved DTII reply rows 21-28,
including the low-halfword-first wire order. `mt_reply_tick` validates the entire
reply before ACK handling, rejects other epochs, and only accepts an ACK with a
fresh heartbeat. A repeated reply cannot retire a later packet. After the first
valid epoch-bound reply, 900 CF blocks without heartbeat progress reset ownership,
disable routes and clear applied revisions even when the event queue is idle.
Malformed or missing replies do not refresh that deadline. Backward heartbeat
movement (half-range serial arithmetic) immediately faults the stream; natural
counter wrap is supported. Recovery remains explicit and advances the epoch.
The future readiness provider must require a healthy, acknowledged instance; an
unobserved heartbeat is not readiness.

Tests execute the SHARC reply writer and feed its actual output into the ColdFire
ACK code. They cover checksum corruption, reserved fields, epoch mismatch,
replayed replies, heartbeat/CF-clock wrap, idle timeout and record-slot decoding.

The frame hooks are connected: `instrument_write_frame` processes replies and writes
only the instrument data halfwords; the pattern-position owner calls
`_instrument_block` before stock dispatch/render, even when position is invalid.
The wrapper preserves DSP registers and arithmetic state, including SIMD pairs.
The V2 instrument engine/schema callback remains to be implemented. The current module
does not claim keyboard routes. Target tests feed actual ColdFire-produced reset
and eight-note packets to the SHARC validator, and exercise receiver retries,
gaps, reset preemption, consumer rejection and stale epochs. They do not establish
live SPI delivery or the independent-note audio lifetime gate.

The internal CF lifecycle API is in `src/cf/bridge.h`. A future instance owner
first calls `mi_instance_reset`, waits for its acknowledged reset and a healthy
heartbeat, then submits configuration with `mi_config_submit`. `mi_route_enable`
requires both an acknowledged configuration and the V2 readiness provider.
Recovery through `mi_transport_recover` is explicit and requires all keys up.
Without an instrument reset callback, reset cannot be acknowledged and routes stay disabled.

Frame-hook checkpoint: 20 host tests and 116 targeted transport, pattern-position
and loop-display tests passed (two DNII firmware-dependent cases skipped).
Both changed modules rebuilt and passed freshness checks; the local combined
DTII proof image passed firmware preflight. The broader changed-file check remains
incomplete: repository pathcheck reports 256 existing stale references in 78 files,
and its host step requires `pnpm`, unavailable in this WSL environment. Changed-doc
link checks, Python lint/format and module layering passed. No device was flashed.

Checks:

```sh
cargo test -p dkmidi-host
uv run python -m tools.patchkit.patchkit build core/midi-events core/runtime
DIGIKIT_CF_SCRIPT=out/native/target/nogen/release/examples/cf_script \
  uv run python -m pytest tests/patchkit/test_midi_events.py -q
```

The host tests execute the production C implementation, including the runtime dispatcher. The ColdFire component test compiles the same cases for the target and executes them on its instruction core. Neither establishes real USB/DIN latency, stock AMP lifetime, DSP transport coexistence or final audio. Those remain required gates before enabling routes in a release.
