# Persistent instrument registration mailbox

Experimental, optional DTII 1.17 adapter for the V2 instrument load protocol.
By default it answers capability queries and rejects lifecycle work. A trusted
admission flag enables the protected row scheduler described below. It does not
publish audio dispatch or make CANOPY installable. V2 admission remains disabled.

The provider exports `_dl_irows`, a 64-byte mailbox for DSP-link window 3, and
`_dl_iwords`, the address of its 16-word capacity constant. The protected MIDI
block wrapper calls `_instrument_registry_block` before processing MIDI input.
The provider uses no firmware anchors of its own.

## Command protocol version 1

Words are logical 32-bit DSP values, transported by the existing DSP-link encoder.
There is one outstanding command globally. The sender must clear word 0 and await
its transfer ACK, write words 1..15 and await their transfer ACK, then commit word
0 last. Do not begin the next command until the matching registry reply arrives.
The transport ACK alone does not acknowledge installation, initialization or detachment.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49524731` (`IRG1`) |
| 1 | Protocol version, 1 |
| 2 | Nonzero, strictly increasing unsigned sequence; never wraps within a boot |
| 3 | Module owner, 0..31 |
| 4 | Nonzero generation |
| 5 | Operation: 1 QUERY, 2 INITIALIZE, 3 DETACH, 4 INSTALL |
| 6 | Registry row, 0..7 |
| 7..14 | INSTALL: track count, state base, state words, arena start/end, reset entry, executable start/end; other operations: zero |
| 15 | XOR of words 0..14, including the committed magic |

Invalid, incomplete, duplicate or older commands retain the previous reply and
do not advance the sequence. A valid command advances the sequence even when its
operation is unsupported. Retrying a committed command with the same sequence
therefore preserves its original reply. After sequence `0xffffffff`, a reboot
is required before further commands. This protocol is internal and experimental.
Only the trusted loader may submit admitted metadata. Public V2 loading remains disabled.

## Reply

`_instrument_registry_reply` holds eight words, copied into words 4..11 of the
existing `_instrument_reply` by the MIDI reply builder. The outer checksum covers
these words and its commit magic is published last. Existing DSP-link reply rows
carry the result; there is no additional reply allocation.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49525231` (`IRR1`), zero before the first accepted command |
| 1..5 | Echoed sequence, owner, generation, operation, row |
| 6 | Status: 0 completed, 1 unsupported lifecycle, 2 unknown operation, 3 row identity/phase/installation metadata rejected, 4 row callback/metadata fault |
| 7 | Capabilities: 0 while disabled, 7 when trusted lifecycle admission is enabled |

INSTALL, INITIALIZE and DETACH return status 1 while trusted admission is disabled.
When enabled, success is returned only after row work completes. Callers must validate the outer checksum, reply magic and all echoed
identifiers before interpreting status. A stale reply never authorizes freeing
memory or publishing a row.

Processor-component checks live in `tests/patchkit/test_instrument_registry.py`.
The composed-frame tests in `tests/patchkit/test_midi_events.py` exercise the
provider alongside the carrier and verify register preservation. These are
emulator checks; no hardware acceptance is claimed.

## Loader transaction client

The internal no-std `instrument_mailbox` SDK component serializes a whole loader
reservation into per-row commands. It obtains physical row identities from the
registry, including holes left by prior unloads. Clear, body and commit writes
require their own sequence/stage-matched transport acknowledgements. The caller
must drain earlier writes before starting a transaction or abandoning one.

The client verifies the full outer checksum and all five echoed identifiers.
It waits for every row before invoking the loader's publish/release transition.
Timeout or partial rejection retains the whole reservation for a fresh detach;
sequence numbers survive recovery and never wrap. A cancellation prevents any
further row from being scheduled after its pending reply arrives.

Capability bits are bit 0 for reset completion, bit 1 for detachment and bit 2
for row installation. A successful lifecycle reply must include its respective
bit. The provider sets none until trusted admission enables its lifecycle scheduler.
Host client tests use synthetic replies; DSP scheduler tests execute real row work
with synthetic callbacks. Trusted admission, audio dispatch and live SPI scheduling
remain open; this client does not enable V2 admission.

INSTALL is accepted only after the SDK image controller has acknowledged all image
and BSS writes and the plan matches the retained allocator grant and physical rows.
The DSP checks its private snapshot and publishes RESERVED last, without calling a
reset. An occupied row is rejected; reuse requires DETACHED and a strictly newer
unsigned generation. Initialization requires a separate command.

After every row has a matching successful install reply with capability bit 2,
the client returns an opaque ticket-bound `Installed` receipt. The image controller
requires this receipt to advance to initialization; a boolean or transfer ACK is
insufficient. Receipts and client state are valid only for the current DSP boot.
Transport drainage, allocation ownership and callback budgets remain trusted
loader obligations. Partial-install failure retains the allocation until matching successful DETACH
replies arrive for every reserved row. An empty row or an older detached entry can
be retired directly for the cancelled ticket without a callback. Its generation
is retained as a tombstone, so delayed INSTALL cannot resurrect it. A newer
identity, equal generation under another owner, or any occupied row belonging to
another ticket is rejected; rejection never authorizes release. The caller must
drain old writes before cleanup and keep the allocation until all replies succeed.

## Internal V2 reset worker

`src/dsp/lifecycle.s` implements `_ir_reset_instance` for one admitted instance.
It accepts state pointer/word count, an owned state interval, reset entry and an
owned executable interval. All intervals are positive, non-wrapping addresses;
state is four-byte aligned and limited to 1..1024 words per call. State addresses
are bytes; executable addresses are SHARC short words. The caller must validate
allocation ownership and executable sections before supplying these intervals.
Bounds checks alone do not establish ownership or exclude other allocations.

The worker validates every argument before changing memory or calling code.
CREATE clears exactly the requested state extent before reset. REPLACE, RECOVER
and UNLOAD preserve state for the callback to inspect. It enters the V2 reset
callback with R4=state and R8=reason, with I13 carrying the return address. Only a
zero callback result succeeds; negative faults propagate and positive results
are rejected. Failure after callback entry can leave modified state and requires
quarantine, not immediate memory reuse.

The mailbox reaches this primitive through the protected row scheduler only when
trusted admission is enabled. Production still needs validated row metadata and
admission of reset budgets. Clearing is bounded; a callback that fails to return
cannot be preempted by this worker. The caller must pin the documented registers
and preserve callback clobbers. The older CANOPY event-reset hook uses a different
ABI and must not be passed to this worker.

`tests/patchkit/test_instrument_reset.py` executes the worker and a synthetic V2
callback on the SHARC processor core. It checks exact clearing bounds, retained
state on non-CREATE resets, invalid ranges and callback failure propagation.
These worker tests alone do not prove mailbox completion or live device admission.

## Internal DSP row lifecycle

`src/dsp/rows.s` now owns eight 16-word DSP rows. This table is separate from the
window-3 command buffer and is not writable through that window. Trusted future
admission must populate metadata only while a row is unreachable, after validating
allocation ownership, executable ranges, descriptor limits and callback budgets.
The INSTALL handler copies its payload into a private snapshot and invokes the
internal installer. There is still no production caller enabling admission.

The row layout is phase, owner, generation, track count, state base, state words
per track, state arena start/end, reset entry, executable start/end, cursor, last
error, attempted-instance count, and two reserved words. Phases are EMPTY=0,
RESERVED=1, INITIALIZING=2, READY=3, DETACHING=4, FAULT=5 and DETACHED=6. READY
means all initialization callbacks returned; it does not publish audio dispatch.
DETACHED retains the owner and generation as a tombstone; it does not free memory.

`_ir_row_begin` requires a valid row index, owner and nonzero generation. DETACH
may claim EMPTY or a strictly older DETACHED entry as a cleared tombstone for the
cancelled ticket. Other transitions require matching owner/generation. It starts
initialization only from RESERVED, and detachment from reserved, initializing,
ready or faulted rows. Repeated detach requests preserve in-progress work;
already-detached rows return success without another lifecycle tick.
`_ir_row_tick` attempts at most one instance reset per call. A separate completion
tick marks READY or DETACHED after the final callback has returned. A failed
callback records its negative result and marks FAULT without discarding metadata.
Initialization records each attempted instance before calling reset, so partial
writes on failure remain owned. Detachment resets only attempted instances; a
cancel before the first initialization tick needs no callback. Failed detachment
retains the row and may be retried with the same owner/generation.

The row worker inherits the reset worker's context requirements. The protected
boundary below supplies callback register preservation, and the mailbox scheduler
uses it when trusted admission is enabled. Production admission and audio dispatch
remain to be connected.
`tests/patchkit/test_instrument_rows.py` executes these transitions with synthetic
V2 callbacks on the SHARC core; this is not device acceptance.

## Protected callback boundary

`src/dsp/protected.s` provides `_ir_row_tick_protected` for callers already in
non-SIMD mode, such as the shared instrument block wrapper. It returns the row
worker's result in R0 and restores R1..R15, the callback's DAG scratch registers,
B3, pinned length/modify registers, MODE1 and arithmetic status. Both 80-bit
foreground/background multiplier accumulators are saved and restored, including
their guard words. Restoration writes each accumulator's middle word before its
guard word because a middle-word write sign-extends into the guard word.

The wrapper pins linear L0..L5, M5/M6/M7=0/1/-1, M13/M14/M15=0/1/-1, and the
same non-SIMD, round-to-nearest arithmetic mode as the existing voice adapter.
It restores B3 before I3 because setting a DAG base also updates its index.
Callbacks must obey the V2 ABI, preserving registers outside the declared clobber
set and leaving SIMD disabled. Static scratch makes this boundary non-reentrant;
an interrupt must not enter it again. It is not a sandbox for arbitrary code.

The row lifecycle component tests now execute through this boundary. Dedicated
context tests also overwrite all permitted scratch registers and both accumulators,
exercise nested CALL/RTS, and check preservation for success and negative fault
results under different incoming arithmetic modes. The comparison happens after
the wrapper returns, before the test harness empties its own hardware call stack.
Mailbox scheduling now uses this boundary when trusted row admission is enabled.
Production admission and audio dispatch remain disconnected.

## Mailbox lifecycle scheduling

`_ir_lifecycle_ready` is an internal, cold-boot-zero flag outside window 3. Only
trusted admission may set it to exactly 1, after arranging valid owned rows and
callback budgets. Keep it stable while work is pending. No production path sets
it yet. When enabled, query and completion replies report capability bits 0/1.
These bits describe reset/detach support, not audio dispatch or public V2 loading.

On INITIALIZE/DETACH, the mailbox snapshots all five identifiers and consumes the
sequence before starting row work. It invokes at most one protected row tick per
frame. While work is pending, the previous reply stays visible and incoming command
bytes are not interpreted. The active ticket survives a clear or overwrite of the
command buffer. A later command may be handled on the frame after completion;
clients must still obey the single-outstanding-command protocol.

Success is emitted only at READY or DETACHED after every attempted callback has
returned. A rejected row identity/phase reports status 3; a row fault reports 4
and preserves owned metadata. The exact callback fault remains in row word 12.
A retry needs a new sequence; repeating a completed command does not call reset
again. Partial initialization and failed detachment retain ownership until a
fresh, matching detach completes. The loader must still drain earlier SPI writes
before sending detach; this DSP scheduler does not establish transport quiescence.

`tests/patchkit/test_instrument_mailbox_lifecycle.py` executes mailbox reception,
protected row work and the shared checksummed reply together, with test-only
admission and synthetic V2 callbacks. It covers delayed completion, replay,
command-buffer mutation, stale identity, partial failure and the disabled gate.

## Host admission planning

The internal no-std `instrument_admission` component validates an entire reserved
module before returning row metadata. It binds an allocator grant to the exact
reservation ticket and physical rows, accepts only pre-transfer reservations,
and requires an exact type match. Image/state grants must be disjoint, aligned,
and inside the caller's available L2 range (within DTII's 512 KiB L2 bound).
Every type's full state allocation must fit its grant without overlapping another
type. Row arena bounds are narrowed to that type's allocation.

Instances use the descriptor format's eight-byte stride: `align_up(state_bytes,
8)`. The DSP row worker now advances by that stride while clearing only the declared
state words, preserving padding. A 12-byte state therefore advances 16 bytes.
Reset and process entries must fit a full six-byte instruction inside a supplied
executable extent; a whole image including data is not an executable extent.

Admission checks nonzero reset/process budgets, total per-track process cost,
stack/call depth, event/control limits, and reset cost including the clear loop
and caller-supplied wrapper/scheduler overhead. The caller must supply remaining
process capacity and a conservative measured overhead bound. Declared callback
budgets are not runtime enforcement or proof that code meets them.

The planner does not discover ownership, allocate memory, install rows, enable the
lifecycle gate, transfer images or publish dispatch. The trusted loader must obtain
exclusive grants from its allocator, validate executable boundaries from parsed
image metadata, exclude V1/builtin type conflicts, and retain memory through detach.
The load controller can now derive a grant by reserving image/BSS/state in a
caller-borrowed shared allocation ledger. Its memory-backed tickets retain their
owner slot until explicit release after pre-transfer cancellation or fully
acknowledged detach; a stale ticket or changed ledger cannot clear the range.
Component tests compose reservation, admission and mailbox-driven release.
The production loader still needs to call these APIs with its real ledger and
route all V2 teardown away from legacy unreserve; live SPI integration remains open. Failed planning leaves all reservations and DSP state unchanged.

`prepare_packed` composes DKDS6 record and text-range decoding with the allocator
grant and reserved ticket. It derives callback PCs and per-type state bases,
checks descriptor track counts against reservations, and keeps separate reset
and process text bounds. The returned plan remains inert. The live loader must
validate the whole section, resolve keys/imports and exclude builtin/V1 conflicts
before using it; no row installation or SPI transfer is performed by this helper.

The SDK `instrument_image::Transfer` controller binds the exact allocation grant
to an image/BSS write sequence. Each ACK must match the load ticket and destination
span; initialization also requires caller-confirmed row installation. Errors
retain memory through quarantine and detach. This is component-tested sequencing,
not a connected SPI driver: the loader still owns sending, ACK validation against
the live link, transport drainage and row installation.
