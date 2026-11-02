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
5. Routing persistence separately from sound presets, firmware-backed control
   save/load evidence and honest SOURCE support (current runtime admits keyboard
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
