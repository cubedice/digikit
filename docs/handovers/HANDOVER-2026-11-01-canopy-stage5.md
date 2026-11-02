# CANOPY Stage 5: preset storage checkpoint

Requested: complete Stage 5. **Stage 5 remains incomplete.** Stage 4 baseline is
commit `977d144d`. This checkpoint implements explicit presets and fixes a
durability acknowledgement bug; no device was flashed or selected differently.

## Implemented

- Four PRESETS slots under KEYBOARD, deferred UI-task SAVE, explicit LOAD,
  working-versus-confirmed dirty state, failure/retry feedback.
- Versioned 64-byte records, stable control IDs 0-15, complete range and checksum
  validation before mutation. Invalid records cannot partially load.
- Embedded learned shape by value. Preset loads preserve the eight-slot library;
  selecting a different library slot or completing LEARN clears the override.
- Atomic reset boundary on load with existing physical-key tombstones. No routes,
  key/pedal state, tokens, phase, pointers or native TUNE/LEVEL enter presets.
- Storage uses 390 bytes including entry overhead for all four presets and eight
  learned shapes. Shared capacity is checked by the runtime insertion function.
- Runtime explicit flush retries a failed record even after automatic retries
  cleared its dirty flag. Previously a same-value SAVE could return OK without
  a physical write. Unchanged successfully saved records still avoid writes.

## Evidence [D]

- 31 existing keyboard/shape tests passed after integrating presets.
- Nine preset scenarios passed: codec round trip, malformed fields with valid
  checksums, independent slots, UI-only I/O, failed put/flush and retry, reboot,
  embedded shape independence, LEARN isolation, and snapshotting the sound and
  destination at SAVE press rather than at the later write tick.
- Three runtime storage regressions execute the production Rust runtime and SDK
  against a memory block device. Read-only, range and exhausted transient failures
  each require a new write on retry and recover the new value after reload.
- `core/instrument-play` built for ColdFire; 3431 instructions re-decoded.
- `core/runtime` built; 46371 instructions re-decoded.
- Required changed-tests preflight ran: 280 host tests passed, 16 optional tests
  skipped, lint/types passed. Formatting was corrected and rechecked after the
  final scenario addition. Global pathcheck retains pre-existing stale paths;
  changed-document checks pass. No booted emulator acceptance ran for this checkpoint.

## Remaining work required before Stage 5 completion

1. Versioned machine UI/control capability and a bounded effective-control bank.
   Compose with `mf_srcpage`; V1 added machines must retain their one-page rule.
2. SOUND/MOVEMENT encoder routing, native TUNE/LEVEL preservation, read-only
   controls, page memory, 750 ms focus and exact native-header/page-dot geometry.
3. DSP fast telemetry and immutable fragmented details with track/generation/
   revision checks, timeout/retry and actual applied versus requested values.
4. Bounded renderer, actual admitted notes and arp source identity, mode/pedal/
   latch marks, clock/link/fault states and the specified LEARN modal.
5. Firmware-backed control save/load evidence and honest SOURCE support (current runtime admits keyboard
   only; do not expose a nonfunctional sequencer choice).
6. Booted emulator acceptance for page keys, stock views, stale events, focus,
   audio/display agreement and persistence. Hardware readability is still unproven.

Important limits: no stage-5 firmware package or hardware installation is claimed.
The cached preset copy means last *confirmed* save in this boot; the runtime may
retry failed writes independently. Never call the RAM settings value durable.
The four firmware-backed custom controls are not yet unified with setup controls.

Relevant code: `modules/core/adapters/instrument-play/src/cf/preset.{h,c}`,
`control.c`, `keyboard.{h,c}`, and runtime `src/rs/src/settings.rs`.
Read `docs/design/canopy.md` SRC interface and persistence sections before proceeding.

## Routing checkpoint after preset commit `167448ad`

Device routing now has a separate canonical 16-byte record, deferred SAVE ROUTING
and validation before any field is restored. Restoring saved enable waits for
an admitted type/generation and cannot bypass the existing healthy/prepared/
configured handshake. User route edits cancel pending restoration. MIDI routes
remain outside presets. Total namespace use including entry overhead is 424 bytes.
No device or firmware selection changed.

Seven new host scenarios exercise delayed admission, user cancellation, failed
put/flush with retry, corrupt records, valid-checksum malformed fields and sound/
route independence. The preexisting 40 keyboard/preset scenarios remain applicable.
The ColdFire adapter build passes with 3688 instructions re-decoded.
Final changed-tests preflight: 288 host tests pass, 16 optional tests skipped;
layer check, lint, formatting and types pass. Its only failure is the existing
256 stale references in 78 files. Scoped changed-document pathcheck reports zero.

Static integration finding [D]: the current 16-word MIDI reply is **not** four
transport words plus twelve free UI words. `midi-events/src/dsp/reply.s` uses
words 4-11 for registry ACKs, 12 for the bound session, 13-14 for lifecycle state
and generation, and 15 for a checksum. The UI proposal requires a negotiated
new reply kind or multiplexing that preserves all lifecycle/reset consumers;
simply writing the proposed payload would corrupt safety acknowledgements.
The content-side input ABI ends at V4/88 bytes and has no status output sink.
An explicit status extension is needed before the adapter can publish actual
applied DSP state without hard-coding CANOPY's private state offsets in core.

**Stage 5 still incomplete:** this checkpoint closes routing persistence only.
SRC page capability/hooks, renderer, controls, status producer/receiver and booted
acceptance remain necessary. Do not label this a Stage 5 release.

## SRC capability checkpoint, 2026-11-02

`core/machines` now supports an explicit versioned two-page capability, scoped
to added machine type and instance generation. The draw hook preserves the page
index and native vector for a registered implementation. Default added-machine
and stock behavior remain unchanged. No CANOPY registration occurs until its
input and paint adapters are complete. V1 declaration rules are unchanged.

26 ColdFire component cases pass; the DT2 module builds with 404 instructions
re-decoded and the DN2 object is unchanged. Static input tracing also identified
the separate encoder interface at SourcePageView+4; primary View slot 3 must not
be replaced as an encoder handler. See the
[SRC findings](../findings/80-canopy-src-pages.md) for exact measured layouts.

Stage 5 remains incomplete. The next implementation must connect the painter
and input adapter, preserve native TUNE/LEVEL and stored custom values, provide
versioned actual-applied DSP telemetry, and run booted acceptance. Do not expose
two pages merely by calling the new registration function.

Combined firmware linking exposed an earlier preset decoder initialization that
emitted an unresolved libc `memset`. Replaced it with a bounded volatile byte
clear; all sixteen preset/routing host scenarios still pass. The playback CF
object now decodes 3690 instructions. The combined instrument-play/digikit-menu
firmware links successfully as `out/patch/canopy-stage5-foundation.syx`, SHA-256
`ef5c2c59f0c8a19d6f0e5fbb3f787bbb67c603512b06a719c8890724899ddde2`.
This is an integration build, not a booted or install-ready Stage 5 release.

Changed-tests preflight passed host tests, layering, lint, formatting and types;
the global pre-existing pathcheck failure remains. Scoped changed-document
pathcheck and the Python owner's lint pass. No physical device was changed.

## SRC model and presentation implementation, 2026-11-02

The instrument adapter now builds `src-state.c`, `status.c`, and `src-display.c`.
The requested controls share the existing configuration/preset bank through
`instrument_src_context`, `instrument_src_encoder`, and `instrument_src_display`
in `control.c`. The context token invalidates old page/track/generation/mode events;
LEARN consumes hidden encoders. Native TUNE/LEVEL return a delegation result,
never a fabricated replacement parameter scale. PLAY and learned-shape controls
are read-only as specified; SOURCE remains KEYS. Setup/preset/selector edits are
imported into this same requested bank. Pending markers clear only for a matching
generation, effective revision and value, never a transport ACK.

The normalized twelve-word status receiver validates identity, wrapping
publication/revision order, pitch validity (including MIDI zero), unique source
identities, fragment framing and padding. It assembles eleven fragments privately,
retains the last completed bank while newer details arrive, rejects conflicting
duplicates and same-revision mutations, validates LEARN session/capture identity,
and implements independent 150 ms freshness and bounded retry scheduling.
The receiver is **not wired to the MIDI reply channel**. Call it only after a
future transport demultiplexer has authenticated a status message; it is not a
replacement for the lifecycle/reset ACK receiver.

Detail schema 1 now has explicit offsets: controls 0..31; pending chord revision
32; LEARN session 33; capture revision 34; four packed pitches 35; count in bits
0..2, validity mask in 4..7, root in 8..15 and FULL in 16 of word 36; shape
revision 37; zero padding 38..63. Captured pitches are sorted and unique. The
last fragment carries four words plus two zeros. Clock-state/capability bytes
still need a producer-owned definition before clock messages can be formatted.

The pure bitmap painter is bounded to x0..123/y10..63, preserving native header
and page-dot pixels. It provides the two control rows, note names, conservative
exact-set chord names, sorted notes retaining source identity, physical/pedal/
latch underlines and badges, actual arp pitch/highlight, focus and requested/
applied mix positions, and link/fault/save errors. A generated preview lives in
`out/canopy-src-preview.png` (component output, **not** an emulator screenshot).
The existing native LEARN menu now uses note names, slot/count and ROOT instead
of raw MIDI numbers; it is not yet the specified custom opaque SRC modal.

Focused host checks cover controls, stale events, clipping, feedback assembly,
revision wrap, MIDI-zero formatting, duplicate-pitch source identity, and the
real preset/configuration callbacks. Firmware compilation caught jump tables
inside executable sections and compiler-inserted libc calls; local freestanding
compiler options prevent those transformations without weakening the ISA gate.

**Still required to complete Stage 5:** native SRC paint/input attachment and
selection ownership, page keys/memory and redraw timing; a versioned DSP status
output extension and producer; authenticated reply multiplexing that preserves
session/reset/lifecycle guarantees; effective native-parameter save/load evidence;
complete clock/excluded-note formatting and the custom LEARN modal; booted
acceptance. No two-page capability is registered yet. No hardware was changed.

Validation for this checkpoint: 309 host cases passed, 16 optional skips;
layering, lint, formatting and types passed. Required changed-tests preflight
still fails only on the pre-existing 256 stale paths in 78 files. The explicit
adapter rebuild passes with 6111 decoded CF instructions. The combined image
links as `out/patch/canopy-stage5-src-model.syx`, SHA-256
`4e868736dbf93f1a90e50af0c82ce9cfed500b66e0a507de37e48688af79c113`.
This is a linked component checkpoint, not booted SRC acceptance or a release.
`patchkit patch` reused an existing DKO during the first attempts: explicitly
run `patchkit build core/instrument-play` after source edits before patching.

## Authenticated UI transport, 2026-11-02

Implemented the separate version-4 reply envelope in `core/midi-events`:
session token, heartbeat, twelve normalized payload words and checksum. It can
neither bind a session nor ACK MIDI/configuration/registry commands. The optional
DSP mailbox is emitted only for an already-bound ready row on alternating
heartbeats; ordinary replies retain the other slots. The design now documents
this cadence rather than the infeasible original all-fields-in-sixteen-words
proposal. Existing images without a producer continue ordinary replies.

The CF adapter authenticates before copying into a 64-message FIFO. It returns
arrival age, explicitly signals lost fragments and clears queued data on trust
loss/reset. Exact duplicate wire replies cannot enqueue twice. Valid status
frames preserve the last genuine registry snapshot, with its original heartbeat
and checksum, so UI polling aligned to status slots cannot starve registry ACKs.
Status payload bytes cannot manufacture a registry acknowledgement.

Evidence [D]: 35 focused native-CF/SHARC tests pass, covering both frame kinds,
session binding, checksums, duplicate/reordered traffic, queue overflow, wrong
sessions and protected frame context. The CF rebuild decodes 2539 instructions;
the DSP object decodes 693. Required changed-tests ran: 309 host checks passed,
16 skipped, layering/lint/types passed. Its Python formatting failure was fixed;
the pre-existing 256 stale paths in 78 files remain outside this change.
The combined image `out/patch/canopy-stage5-status.syx` links, SHA-256
`396331526acff0ab636aa28293ba810a16934231c35903a42bb967aa81128c48`.

**Stage 5 remains incomplete.** This mailbox has no production producer or UI
consumer yet. Next implement the opt-in process output capability, actual DSP
telemetry/effective bank and UI-clock freshness handoff. Do not have the adapter
inspect private CANOPY state or present queued old messages as newly fresh.
Native SRC view/input integration, complete modal/clock presentation, native
parameter persistence and booted acceptance listed above are still required.
No physical device was changed and this image is not a Stage 5 release.

## Opt-in process status output, 2026-11-02

The borrowed-output capability is implemented end to end. Descriptor flag bit
0 is preserved by decoding, physical row mapping and process-install metadata;
unknown bits are rejected before publication. DSP rows retain flags separately
without changing the existing eight-word process-row layout. V5/96-byte inputs
are accepted only with an installed status capability and matching READY row
identity. The last two words contain the adapter-owned 13-word output pointer
and exact capacity. Alignment, L2 extents and input size are checked before
calling content. Ready is cleared before invocation and after negative/invalid
results. The adapter clears it every block before admission/session checks.
Unflagged modules continue receiving the prior V3/V4 layouts.

The playback adapter now owns `_instrument_ui_status`, linking the negotiated
output to the authenticated wire transport. CANOPY remains unflagged: its
actual-note producer and coherent applied-control/mapper metadata are still
required. Do not enable the bit while its callback accepts only V2..V4, and do
not infer PLAY/ROOT/LEARN from notes or treat CF requested values as DSP-applied.
The UI consumer, wall-clock freshness and native SRC attachment remain open.

Evidence [D]: 133 SDK instrument tests pass; 71 DSP lifecycle tests pass;
39 process/playback tests pass with one unrelated CF runner test skipped;
the added opted-in playback/session-revocation scenario passes separately.
Adapter builds decode 1470 registry and 989 playback DSP instructions;
runtime rebuild decodes 46374 CF instructions. The combined image links at
`out/patch/canopy-stage5-output.syx`, SHA-256
`8d5ec4071c5b1ced631249d492b5ed679be87a477e115809c7873bb0b8412c10`.

The SDK header was regenerated from its source. The broader generator stops
in an unchanged firmware hook on `dsp.voice.hook.velocity_operand: unknown
register "s7"`; initial stale header reports also included line-ending-only
differences in machine/fx headers (no content change retained). Do not report
the repository-wide generator as passing. No booted acceptance or physical
device modification was performed for this checkpoint.

Required changed-tests preflight: 309 host cases passed, 16 optional skips;
lint, formatting, layering and types passed. Only the existing global pathcheck
fails (256 stale paths in 78 files); the two changed documents pass scoped
pathcheck. The final descriptor test also passes after its readability edit.
