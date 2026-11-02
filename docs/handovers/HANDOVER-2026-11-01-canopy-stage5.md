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
