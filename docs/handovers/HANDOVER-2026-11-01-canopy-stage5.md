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

## Shared configuration revision, 2026-11-02 (in progress)

CPS6 now carries the existing sixteen DSP controls plus the sixteen stable SRC
values. Header low byte is 6; the remaining bits hold mapper mode 0..2. The
adapter and CANOPY validate ranges, duplicated values and the milliseconds-to-
blocks STRUM conversion before mutation. CANOPY declares max_controls=32;
CPS1/3/4/5 remain supported. Arp and strum accept the new control count.

The CF owner creates both banks from one locked preset snapshot and submits the
SRC revision, rather than inventing an unrelated transport revision. Metadata
(mode/clock/family/BPM) advances the same revision; identical snapshots do not.
Configuration receipts still cannot clear pending markers without DSP feedback.
This does not yet implement the actual-note/status producer or native SRC view.

Evidence so far: 27 focused direct/protected DSP cases pass, including sample-
and-state equality against CPS5, contradictory UI-bank rejection and atomic
configuration replacement. The 39 focused preset/SRC host cases pass. Explicit
builds pass: playback DSP 1094 instructions, CF 6149, CANOPY DSP 2656. The CANOPY
callback fixture now supplies the process-flags table added by the prior ABI
change. Production CANOPY remains unflagged for V5 status output.

The broader CANOPY callback/playback regression command was rejected by the
user at the execution approval boundary and was not run. Do not retry that
denied command without renewed authorization. Required changed-tests, complete
regression, combined-image/package checks and booted acceptance have not run
for this configuration change. No physical device was changed. Stage 5 is
still incomplete; do not present these component checks as release acceptance.

## 2026-11-02: bounded SRC status consumer and presentation bank checks

The display bridge now drains authenticated `mi_status_reply` traffic through
`is_poll`. It never timestamps a queued message as fresh at dequeue time. First
selection, a UI gap of 150 ms, overflow, trust loss, or an unbounded drain clears
published/partial status. A previous empty poll provides a conservative arrival
timestamp; audio-frame age supplies an additional rejection check. Native SRC
integration must still supply an independent millisecond clock. The callback
frame counter and UI-loop iteration counter do not satisfy that requirement.

Configuration acceptance now checks the installed module's control capacity
before replacing its current bank/revision. An older 16-word module therefore
rejects CPS6 without losing its last usable configuration. The CF bank explicitly
initializes all 32 words: a partial aggregate initializer generated an unresolved
`memset` at firmware link despite the loop-distribution pragma.

SRC cells now format musical rate names (including explicit 5/Q and 7/Q), master
step counts, milliseconds, octave counts and percentages. Swing reflects the
existing DSP's 100+n / 100-n ratio, rounded only for display, rather than claiming
the earlier proposed 75% ceiling. Compact VOICE/OCTS labels, centered values,
pending-chord/excluded-key text, centered latch-only marks and focus suppression
of the note strip are implemented in the bitmap renderer. These remain component
rendering changes, not a claim of native-view acceptance.

Validation: 24 SRC host cases, 19 preset host cases, and 6 focused SHARC
configuration cases pass. Required changed-tests passes 315 host cases (16
skipped), lint, formatting, types and layers; its global pathcheck still reports
the existing 256 stale paths across 78 files. Scoped checks of the changed design
and handover report zero stale paths. Logs use `out/canopy-stage5-poll-*`,
`out/canopy-stage5-capacity.log`, `out/canopy-stage5-display-final.log` and
`out/canopy-stage5-bank-final.log`. The previously denied full CANOPY callback
regression was not retried. No physical device operations were performed.

Next required work remains the CANOPY V5 actual-state producer, physical-key and
LEARN metadata ownership, native SRC painter/input/clock hooks, unified native
TUNE/LEVEL handling, and booted Stage 5 acceptance. CANOPY still does not publish
the two-page capability or opt into V5 output; do not mark Stage 5 complete.

Final explicit builds: CF playback 6375 decoded instructions, DSP playback 1098,
CANOPY DSP 2656. Bounded volatile string measurement also avoids an unwanted
`strlen` import in the firmware renderer. Both owner host suites were rerun after
the link fixes. Combined image `out/patch/canopy-stage5-presentation.syx` links
successfully (2,010,016 bytes, SHA-256
`f901490a94bae60bbc2d0fdbe6a3c4edc6b423dd2390ad48061178ca2b33cff3`).
It has not been booted for acceptance or installed on hardware.

## 2026-11-02: CANOPY audio-side status producer

Added `modules/machines/canopy/src/dsp/status.s`, called only after successful
finite-output validation. V5 validates its borrowed sink and clears ready before
configuration/event validation; publication sets ready last. V2/V3/V4 remain
supported. Fine synchronized arp accepts V5's unchanged V4 phase fields.

For CPS6, the producer emits admitted pitches with explicit validity and stable
slot IDs, actual arp source/pitch/gate matching, applied blend, release-tail count,
and the applied control revision/bank. Release tails never join the chord pool.
The instance's previously reserved word188 holds an 88-block cursor: fast and
detail choices each last four callbacks. Thus either parity of the alternating
wire reply observes all eleven fragments and a fast message within eight blocks,
while completed banks remain below the 25-per-second ceiling.
Control/detail revisions change together; no receipt alone confirms settings.

This is deliberately NOT enabled in the production descriptor. Physical key,
pedal and retained-latch fields cannot be inferred from DSP gates; those fields
are currently zero, not a complete ownership report. LEARN capture and complete
clock-state/capability telemetry are also absent. Do not expose these partial
frames as complete product status. Next work should correlate DSP token identity
with the CF mapper (without substituting requested notes for admitted notes),
complete those fields, then enable descriptor bit0. Native SRC hooks and running
firmware acceptance still remain. Stage 5 is incomplete.

Evidence: 38 focused direct/protected callback tests pass, covering the new output,
V4 timing and CPS6 bank validation; both wire parities reconstruct the bank. Ten
producer tests pass with sink canaries, including exact sample/state equality to
V4 and ready clearing on rejection. Seven existing focused playback adapter
tests pass. An additional real-CANOPY protected-carrier test passes, including
status revocation on session loss. Its initial fixture sent MIDI revision1 after
configuring revision7 and was correctly rejected; the corrected fixture uses
one revision throughout. Logs: `out/canopy-stage5-producer*.log`.

Explicit CANOPY build passes with 2940 decoded DSP instructions. The disabled-
capability package `out/patch/canopy-stage5-producer.dkm` packs successfully:
22,036 bytes, SHA-256
`43e8e40c6fea3f2ace0bd028a36e7ab6d3feabb083facdf9d01e9ae280a95da0`.
No physical device was changed.

Final producer review corrected the initial 44-block cycle to 88 blocks to
respect the completed-detail rate limit. All eleven producer/carrier tests pass
after that correction. Changed-tests passes formatting and layering; its lint
finding (explicit `zip(strict=False)` for adjacent pairs) was fixed and the
owner lint/format checks pass. Global pathcheck remains the same 256 pre-existing
stale references in 78 files; scoped handover pathcheck passes. The broader
previously denied callback regression was not rerun.

## 2026-11-02: token-bound ownership integration (current work)

DSP instance allocation is now 800 bytes (200 words), with the core admission
ceiling updated to match. Reset/replace clear the new metadata. Words189..192
hold admitted tokens,193 the detail/chord observation revision,194 the control
revision. A changed token set or control revision advances the observation
revision and restarts detail transmission; unchanged frames retain the revision.
Every fragment therefore describes the same token/control set. Same-pitch
replacement still changes identity. DSP schema2 adds tokens at detail words38..41;
the remainder stays zero. Schema1 remains accepted by the CF receiver, but its
frames cannot supply token-based ownership. Mixed-schema assemblies and duplicate
nonzero tokens are rejected.

`cv_ownership` joins confirmed schema2 tokens against current mapper voices by
full token AND pitch, then their source-key tokens. It handles duplicate pitches
independently and refuses another generation/mode. The UI bridge calls it under
the mapper lock; no borrowed pointer survives. It supplies held/pedal/latch
underlines and physical pedal/retained-latch badges without treating audio tails
as held keys. A replaced, not-yet-confirmed voice receives no ownership mark.

Checks completed: 13 focused producer/reset/carrier DSP tests; two additional
direct/protected immutable-token-revision cases; 44 SRC/preset host tests.
Explicit builds pass: CF playback6552 and CANOPY DSP2985 decoded instructions.
Logs are `out/canopy-stage5-identity-{dsp,tokens,host,build}.log`.
Required changed-file checks and combined-image link remain to be completed for
this chunk. The current changes have not been committed yet.
Changed-tests was started with explicit changed paths in execution session59330;
inspect that session or `out/canopy-stage5-identity-changed.log` before launching
another check or build. It was still running at the last observation.

Stage5 is still open: LEARN metadata/modal, complete clock-state reporting,
native SRC view/input/independent clock, native TUNE/LEVEL integration and booted
acceptance remain. Production descriptor capability remains disabled. No device
operations were performed.

## 2026-11-02: clock reporting, LEARN actions and independent UI time

Token ownership checks completed: required changed-tests passed host/lint/types/
layering but retained the existing global pathcheck failure. The identity image
linked successfully (SHA256 cdd610cc0f5a17c54befd879547195f20e18a8581fd8f118c8718665ed31bf7c).
That intermediate image predates the following changes.

V5 timing flags now retain low valid/running bits and add coherent grid/phase
provider bits8/9 and explicit stopped bit10. Only V5 receives those bits;
V2-V4 remain unchanged. Missing frames do not masquerade as STOP. CANOPY masks
those flags before scheduling, requires the fine provider for V5 PHASE, and
publishes LOCAL/WAIT/RUNNING/LOST/STOPPED plus provider capabilities. Instance
words195/196 track acquired sync and selected clock mode. The display suppresses
arp highlights during WAIT/LOST/STOP and retains pad-note labels. Tests cover
initial absence, acquisition, loss, explicit stop, restart and absent providers.

LEARN now binds YES SAVE to every informational row; NO still cancels. A stale
capture refreshes before a second YES can commit it. Successful working-memory
commit immediately says LEARNED - UNSAVED; durable completion retains its
separate success/failure notification. Five informational rows replace the old
seven-row list with selection-dependent SAVE/CANCEL actions.

[D] Independent UI clock: MAIN function400cc8b2 extends DTCN0 at0xfc07000c
into high/low words42940a4c/42940a50, retaining the previous counter at42940a54.
Function400cc970 divides the accumulator by132000000 for seconds. Registration
at400cca0c passes the update callback and interval128 to40002aa4. The earlier
Stage4 passive hardware readings independently measured approximately132MHz.
The adapter now reads high/low/previous/current under the IRQ mask, restores
interrupts, then converts to milliseconds with bounded unsigned division.
It includes unaccumulated counter time and does not modify firmware state.
Native SRC display/encoder bridges use this clock; host fixtures retain injected
time. Full-width conversion preserves hardware-counter and millisecond wrap.
Logs: out/canopy-stage5-time-{refs,disasm,callers,registration}.log.

Validation:17 focused DSP/carrier checks;56 keyboard/preset host checks;27 SRC
host checks including timer conversion and wrap. Builds:CF6665,playbackDSP1116,
CANOPY DSP3048 decoded instructions. Latest link/pack and changed checks are
pending below. No physical device changes. Production status capability remains
OFF. Native SRC painter/input attachment, queued-event context capture, native
TUNE/LEVEL/custom-parameter integration, LEARN wire metadata and booted Stage5
acceptance still remain; Stage5 must not be marked complete.

Final local link:out/patch/canopy-stage5-clock-learn.syx,2010912bytes,SHA256
`e03c50e91bf1bd06e16a49d7dc033140a89e2877a64dc2fae85f543225898202`.
Package:out/patch/canopy-stage5-clock-learn.dkm,22660bytes,SHA256
`9c146dffd055baa6c923e9cf60f40e2b18a4db05689ca5c2214874a4420f884d`.
The final acquisition/loss/stop/recovery test also passes for both GRID and
PHASE, direct and protected (four cases). Native selected-track scalar was
resolved by static consumers:4004abb8 returns the selected record's word8;
40035cca treats it as0..15 and computes kit+0x6c+index*0x3c0. See finding80.

Final checks: changed-file preflight passed64 related host cases, layering,
lint, formatting and types; it failed only the existing global pathcheck
(256 stale references in78 files). Scoped checks of all three changed documents
report zero stale paths. Focused Python lint passes. The missing-phase-provider
check passes direct/protected (two cases). No check remains running.
