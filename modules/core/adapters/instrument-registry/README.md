# Persistent instrument registration mailbox

Experimental, optional DTII 1.17 adapter for the V2 instrument load protocol.
It answers capability queries only. It does not publish instruments, call reset
callbacks, dispatch audio, or make CANOPY installable. V2 remains disabled.

The provider exports `_dl_irows`, a 64-byte mailbox for DSP-link window 3, and
`_dl_iwords`, the address of its 16-word capacity constant. The protected MIDI
block wrapper calls `_instrument_registry_block` before processing MIDI input.
The provider uses no firmware anchors of its own.

## Command protocol version 1

Words are logical 32-bit DSP values, transported by the existing DSP-link encoder.
There is one outstanding command globally. The sender must clear word 0 and await
its transfer ACK, write words 1..15 and await their transfer ACK, then commit word
0 last. Do not begin the next command until the matching registry reply arrives.
The transport ACK alone does not acknowledge initialization or detachment.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49524731` (`IRG1`) |
| 1 | Protocol version, 1 |
| 2 | Nonzero, strictly increasing unsigned sequence; never wraps within a boot |
| 3 | Module owner, 0..31 |
| 4 | Nonzero generation |
| 5 | Operation: 1 QUERY, 2 INITIALIZE, 3 DETACH |
| 6 | Registry row, 0..7 |
| 7..14 | Reserved, must be zero |
| 15 | XOR of words 0..14, including the committed magic |

Invalid, incomplete, duplicate or older commands retain the previous reply and
do not advance the sequence. A valid command advances the sequence even when its
operation is unsupported. Retrying a committed command with the same sequence
therefore preserves its original reply. After sequence `0xffffffff`, a reboot
is required before further commands. This protocol is bootstrap-only; lifecycle
payloads and capability bits must be specified before enabling V2 loading.

## Reply

`_instrument_registry_reply` holds eight words, copied into words 4..11 of the
existing `_instrument_reply` by the MIDI reply builder. The outer checksum covers
these words and its commit magic is published last. Existing DSP-link reply rows
carry the result; there is no additional reply allocation.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49525231` (`IRR1`), zero before the first accepted command |
| 1..5 | Echoed sequence, owner, generation, operation, row |
| 6 | Status: 0 query accepted, 1 unsupported lifecycle, 2 unknown operation |
| 7 | Capabilities, currently zero |

INITIALIZE and DETACH always return status 1. No successful reset or detach is
implied. Callers must validate the outer checksum, reply magic and all echoed
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

Reserved future capability bits are bit 0 for reset completion support and bit 1
for detachment support. A successful lifecycle reply must include its respective
bit. The bootstrap provider sets neither and rejects lifecycle commands, so it
cannot complete these transactions. Successful lifecycle tests use synthetic
replies. Reset payloads, callbacks, dispatch and live SPI scheduling remain open;
this client does not enable V2 admission.

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

This is an internal primitive, currently unreachable from mailbox commands.
Before connecting it, the adapter still needs validated row metadata, admission
of reset budgets, protection of the full callback register context, and work
scheduling across tracks. Clearing is bounded; a callback that fails to return
cannot be preempted by this worker. The caller must pin the documented registers
and preserve callback clobbers. The older CANOPY event-reset hook uses a different
ABI and must not be passed to this worker.

`tests/patchkit/test_instrument_reset.py` executes the worker and a synthetic V2
callback on the SHARC processor core. It checks exact clearing bounds, retained
state on non-CREATE resets, invalid ranges and callback failure propagation.
No mailbox lifecycle success or live device admission is implied.

## Internal DSP row lifecycle

`src/dsp/rows.s` now owns eight 16-word DSP rows. This table is separate from the
window-3 command buffer and is not writable through that window. Trusted future
admission must populate metadata only while a row is unreachable, after validating
allocation ownership, executable ranges, descriptor limits and callback budgets.
No admission path populates it in the production adapter yet.

The row layout is phase, owner, generation, track count, state base, state words
per track, state arena start/end, reset entry, executable start/end, cursor, last
error, attempted-instance count, and two reserved words. Phases are EMPTY=0,
RESERVED=1, INITIALIZING=2, READY=3, DETACHING=4, FAULT=5 and DETACHED=6. READY
means all initialization callbacks returned; it does not publish audio dispatch.
DETACHED retains the owner and generation as a tombstone; it does not free memory.

`_ir_row_begin` requires a matching owner/generation and valid row index. It starts
initialization only from RESERVED, and detachment from reserved, initializing,
ready or faulted rows. Repeated detach requests preserve in-progress work.
`_ir_row_tick` attempts at most one instance reset per call. A separate completion
tick marks READY or DETACHED after the final callback has returned. A failed
callback records its negative result and marks FAULT without discarding metadata.
Initialization records each attempted instance before calling reset, so partial
writes on failure remain owned. Detachment resets only attempted instances; a
cancel before the first initialization tick needs no callback. Failed detachment
retains the row and may be retried with the same owner/generation.

The row worker inherits the reset worker's context requirements and is currently
unreachable from mailbox commands. Callback register protection, admission,
mailbox scheduling/acknowledgements, and audio dispatch remain to be connected.
`tests/patchkit/test_instrument_rows.py` executes these transitions with synthetic
V2 callbacks on the SHARC core; this is not device acceptance.
