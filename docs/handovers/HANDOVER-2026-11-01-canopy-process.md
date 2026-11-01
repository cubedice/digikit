# CANOPY Stage 1: process dispatch checkpoint

The user asked to complete Stage 1. It is **not complete**. Do not mark fixture
or processor-only success as a playable V2 machine. The exact open checklist is
in [the design](../design/canopy.md#stage-1-completion-checklist).

## Implemented

- SDK admission retains process entry/type/capacities. Mailbox installation now
  requires op 4 reset metadata and op 6 process metadata with separate ACKs,
  sequence reservation and failure recovery before CREATE/publication.
- DSP process metadata is bound to a RESERVED owner/generation. Protected process
  dispatch requires READY state and derives each instance from the admitted stride.
  Callback faults quarantine the row, zero output and retain memory for retirement.
- CANOPY V2 callbacks take normalized events and allocator-owned 528-byte state.
  The legacy atomic voice/pedal engine now has an explicit-bank entry shared by
  both callback paths. Neutral tuning and fixed PLAY controls are required.
- Core registry includes process dispatch. Public loader/packager still reject V2;
  there is no CANOPY machine manifest/package or production route yet.
- Booted load fixture has separate uploaded reset/process functions and checks
  a process write, instance call count and later DETACH before memory release.

## Checks and limits

167 SDK tests passed after the protocol change. 31 mailbox/lifecycle processor
checks passed. The legacy packet engine's 23 tests passed after the explicit-bank
refactor. All 22 CANOPY V2 direct/protected cases and 24 dispatcher cases pass;
36 layercheck tests and the repository layer check pass. The booted uploaded process test passes (89.26 seconds), including output, state
mutation and acknowledged retirement. Core artifacts were rebuilt/current-checked. Logs are `out/canopy-process-*` and
`out/canopy-v2-*`. The scoped documentation check has no stale links. Global
pathcheck retains the known 256 stale references in 78 files.

Held C/G oscillator state/gain matches a paired reference instance throughout
E onset/release in processor tests. This does not prove final stereo output.
The V2 descriptor budgets remain provisional; do not enable release admission.
Reset clears state immediately: production carrier quiescence/ramping must precede
it. Dispatcher buffers are trusted adapter-owned buffers, bounded by bootable L2
(excluding ROM workspace); image/state allocations retain their smaller bounds.
Invalid dispatch before callback does not clear an untrusted output pointer:
the adapter must preclear scratch and handle faults.

## Next required integration

1. Connect normalized event/configuration queues to admitted instances, with
   route ownership/generation/revision and precleared audio scratch.
2. Connect real machine registration and explicit keyboard route to shared carrier
   ownership, replacing the stock type-1 fixture shadow.
3. Wire production loader/session/allocator and capability gating; keep V2 pack
   refusal until those paths are actually supported. The current runtime probe
   uses an empty legacy ledger and is not a production allocation policy.
4. Run Stage 1 acceptance through the integrated admitted path: final stereo
   C/E-add-G-release-E continuity across transition samples, pedal, independent
   velocity and fault retirement. Existing carrier final-output proofs use the
   old fixture, so they do not close this gate.

No hardware firmware deployment occurred in this work.

## Preflight disposition

The changed-file preflight passed Python lint/format/type checks, layer checking,
related host-test selection, TypeScript lint and formatting. Global pathcheck
still reports the existing 256 stale references. The unrelated repository
`test_verify.py` sweep was interrupted after 287 seconds, with 132 passing tests
and the two already-known failures: unclassified `patch:toolchains` task and
`tools/device/dsp_reset_probe.py`. It is not a clean full preflight or merge gate.
The targeted new/changed behavior checks completed separately as listed above.

## Follow-up: transport input boundary

`src/dsp/input.s` in core/instrument-registry now supplies `_ir_process_packet`
and `_ir_normalize_packet`. They validate/normalize an ordered packet into the
borrowed V2 ABI, preclear output, reject stale track/generation/revision/epoch,
and render event-free blocks without replaying a prior batch. Production receiver
ordering and route ownership are still required from the caller. The core object
is rebuilt/current-checked (1355 decoded instructions).

The CANOPY processor suite now has 30 passing cases, including packet-to-instance
notes, sustain, bend, full bursts, stale/corrupt packets and atomic late-event
rejection. The 36 layer tests pass. The booted load fixture now uses this packet
path before its uploaded callback and checks the normalized six-word note record;
it passes in 84.93 seconds (`out/canopy-input-live.log`). This is a diagnostic
callback, not CANOPY final-output acceptance. Stage 1 remains open; the production
loader, machine/route selection and shared-carrier connection remain the next work.

## Follow-up: route readiness lifetime

The MIDI adapter now revalidates instance readiness before accepting a routed
channel message and before preparing each outgoing frame. A module losing readiness
revokes that track's routes and requests the reserved reset while keeping held-key
tombstones. Idle keyboards cannot leave queued old notes flowing into a retired
instance. Readiness callbacks are now explicitly read-only and IRQ-safe; the frame
loop calls at most once per enabled track. No public V2 route provider is enabled.

The production C behavior passes all 23 host cases and the ColdFire ownership/dispatch
component test, including revocation without an explicit owner disable, unrelated
track isolation, release pairing and quiet-keyboard frame-time revocation. The MIDI
core object is rebuilt/current-checked (2008 decoded instructions). Logs:
`out/canopy-route-readiness.log`, `out/canopy-route-coldfire.log`,
`out/canopy-route-build.log`. Production loader and carrier connection remain open.

The booted stock-carrier chord regression also passes (158.88 seconds;
`out/canopy-route-chord.log`): C/G retain phase and gain when E is released.
This remains fixture regression evidence, not integrated V2 acceptance. Scoped
documentation path checks pass. Changed-file preflight passes its other selected
checks but still fails the existing global 256 stale references in 78 files.
