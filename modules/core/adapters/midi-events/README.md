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
CANOPY supplies proof-only callbacks in its uninstalled source tree; normal firmware still has no instrument callbacks. Missing or rejecting
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
release schema callback remains open; CANOPY currently accepts only its internal fixed-sine proof schema. Tests use a publisher fixture
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
The public V2 descriptor, release schema and live instrument integration remain open. The current module
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

The internal `src/dsp/carrier.s` implements a per-track carrier transition policy:
owned tracks emit one wake on QUIET-to-ACTIVE, suppress stock key/sequencer edges,
and emit one release on ACTIVE-to-QUIET. ACTIVE includes local release tails and
pending replacements. Unowned tracks pass stock flags through and forget previous
V2 history. Invalid track indexes pass through without accessing the state bank.
Call once per track per block and use the returned flags for both stereo voices;
calling separately for each voice would consume the edge on the first voice.
`_mi_carrier_publish` now performs that single transition and stores a pair with
the current block stamp. `_mi_carrier_flags` reads it without consuming it for
voice 2t or 2t+1; stale, missing or unowned pairs pass that voice's stock flags
through. Publish must finish before either envelope call. The caller must supply
the same block stamp to publish and both reads, and must publish only once.
The owner must complete retirement/reset before relinquishing ownership; this
helper does not silence the kernel or configure the stock envelope.

`src/dsp/envelope.s` now interposes the DT2 envelope entry. It derives the voice
from the configured envelope-record base, substitutes only the two owned control
arguments, restores touched registers/status (including SIMD pairs), executes the
displaced stack adjustment and jumps back into stock code. Missing, stale,
disabled or non-record inputs leave stock arguments unchanged. The frame owner
must advance the block stamp before the call. `workspace.s` captures the bank
from the dispatcher's live I1 pointer, executing the displaced byte-offset
MODIFY before storing its result. It preserves R/S registers and arithmetic
status, including with SIMD enabled. The envelope hook is non-reentrant.

The normal block wrapper now calls optional `_mi_carrier_begin` before receiving
events. When linked, `carrier-frame.s` advances the block stamp and clears all
publication-valid bits, including at counter wrap. Transition history survives.
CANOPY's internal raw render entry calls `_mi_carrier_rendered` after rendering;
this publishes once for both stereo voices and retains ownership through reset
tails and their final release edge. The kernel reports ACTIVE through the block
containing its last tail samples, so the shared envelope cannot release early.

The carrier sources remain absent from the normal object list; the optional call
is skipped when absent. Instruction-core tests connect the actual block wrapper,
raw voice adapter, render publisher and envelope interposer in one memory image.
Their receive/reply callbacks are stubs; their workspace pointer is synthetic.
The separate firmware probe patches both the workspace derivation and envelope
entry in private memory. It executes the original dispatcher setup and checks
that capture matches the stock computed pointer, including after relocation.
Four hash-resolved firmware sites and two replace hooks now declare these
boundaries. Normal manifest installation, stock envelope configuration and final
mixer output remain unproved. Readiness and MIDI routes remain disabled.

The block wrapper also calls optional `_instrument_carrier_prepare` after receive.
The CANOPY proof provider snapshots admitted tracks and actual kernel tails/pending
notes before stock gain construction. `velocity.s` selects stock curve 0's 127/128
factor only for owned, current-frame stereo voices. `velocity-entry.s` interposes
the scalar gain builder after its velocity read, preserving the stock latch and
all other operands. Frame start expires these snapshots explicitly. The new gain
interposer is tested in private firmware memory and has two hash-resolved sites
and a replace-hook declaration. Its provider and helper sources remain outside
the normal object list; no manifest installs it. The musical proof connects
kernel, AMP and scalar gain while varying the shared velocity cache every block.
It does not establish the live scheduler or final mixer output.

```sh
cargo test -p dkmidi-host
uv run python -m tools.patchkit.patchkit build core/midi-events core/runtime
DIGIKIT_CF_SCRIPT=out/native/target/nogen/release/examples/cf_script \
  uv run python -m pytest tests/patchkit/test_midi_events.py -q
```

The host tests execute the production C implementation, including the runtime dispatcher. The ColdFire component test compiles the same cases for the target and executes them on its instruction core. Neither establishes real USB/DIN latency, stock AMP lifetime, DSP transport coexistence or final audio. Those remain required gates before enabling routes in a release.

The CF reply validator accepts the optional IRR1 registry block in words 4..11;
version 1 reserves words 12..14 as zero, while version 2 carries a nonzero
session token in word 12 and reserves words 13..14. The checksum covers all fields.
`mi_registry_reply(out, words)` copies the last framed registry reply under the
frame IRQ lock: 16 words on success, 0 if unavailable, -1 for a null output or
capacity below 16, and -2 when registry boot trust is lost. Unsuccessful calls
leave output untouched. Missing or corrupt
frames and explicit recovery invalidate the cache. MIDI epoch admission remains
independent, so registry bootstrap can precede MIDI readiness. The caller must
validate transaction identity and freshness; this getter does not enable routes
or prove a live V2 module.

Registry reply trust uses its own heartbeat guard, independent of MIDI epoch
admission. A valid backwards heartbeat, a non-advancing/missing heartbeat for
`MT_STALL_BLOCKS`, or explicit MIDI transport recovery permanently invalidates
registry snapshots until a cold CF boot. Valid heartbeat wraparound is accepted;
corrupt replies cannot supply a new heartbeat. The getter returns -2 without
copying old bytes after invalidation. This is conservative loss of trust, not a
unique reboot detector. It cannot detect an unobserved reset whose new counter
looks forward. If frame callbacks stop entirely, UI-task deadlines still apply.
The SDK treats -2 as permanent boot invalidation and blocks the mailbox exchange
without issuing a detach into the unknown boot or releasing its storage.


`mi_registry_expect_session(token, sequence)` arms one expected bind identity under
that same IRQ lock and clears the cached reply. Zero arguments return -1 without
mutation; a second arm or revoked guard returns -2. It sends no command and grants
no lifecycle or MIDI capability. Before arming, unexpected version-2 replies
revoke trust. While armed, version-1 replies may maintain heartbeat liveness but
cannot provide registry snapshots or extend the fixed 900-frame bind deadline.

The first accepted version-2 reply must have a fresh heartbeat, the expected
nonzero token and exact successful BIND_SESSION ACK: sequence, owner/row zero,
generation equal to token, operation 5, status/capabilities zero. Subsequent ACKs
may change, but every framed reply must retain the token and version. A mismatch,
downgrade, backward heartbeat or expired deadline revokes trust permanently.
Malformed replies do not establish identity or refresh liveness; they lead to
timeout if valid replies do not resume. No late frame revives an expired guard.

These checks do not implement the loader bootstrap transaction or prove reset
coverage. The SDK mailbox and MIDI event ACK path still reject version 2; V2
admission stays disabled. The eventual loader must also apply a UI-clock deadline
when frame callbacks stop, keep the expectation armed before its first bind
write, and never retry a possibly committed bind after an uncertain outcome.
