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
