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

## Follow-up: shared DSP allocation

The V2 lifecycle path now obtains its pool bounds from the runtime dyndsp provider
and borrows the real mod_off/mod_len ledger through dsp::instrument_ledger. The
probe no longer supplies an address or assumes an empty 256-byte pool. It refuses
an occupied owner record and holds the link lease through acknowledged release.

Booted integration exposed an allocator mismatch: the actual 512-KiB shared pool
starts above the L2 origin, extending beyond the lower executable region. V2 now
validates the complete ledger within bootable L2, including upper-L2 legacy
reservations, but still requires its own entire allocation below 0x20080000.
A full lower region fails without changing the ledger or consuming the ticket.
Seven allocator tests pass, including upper-L2 reservations and lower-region
exhaustion. The diagnostic fixture explicitly depends on core/dyndsp.

The booted test seeds a 512-byte reservation in the actual runtime ledger, then
checks aligned V2 placement after it, uploaded reset/process callbacks, and detach
before release. The seeded reservation models occupancy; it is not a running
legacy module. Normal DKM loading, CF publication after CREATE, public capability
gating and machine/route/shared-carrier integration remain open.

Validation: the full booted shared-ledger lifecycle test passes
(`out/canopy-shared-ledger-live.log`), as do seven allocator tests, TypeScript
type/format checks and scoped documentation checks. The runtime object is rebuilt
and current-checked at 43690 decoded instructions. Changed-file preflight still
fails only the existing 256 stale references in 78 files. No hardware was touched.

## Follow-up: CF publication failure cleanup

The normal loader publication routine is now publish_cf: it owns CF registry,
init and slot publication only. It no longer releases DSP memory on failure; the
existing legacy caller cancels its unsent reservation. A future V2 caller must
wait for CREATE before publication and detach before reclaiming storage. That
asynchronous normal-file loading path is still not implemented.

Fixed a real normal-load bug: init could register service command handlers and
then fail, leaving pointers into the freed CF image. Publication failure now
removes those handlers before registry cleanup and caller-owned reclamation.
The new test/svc-init-fail fixture returns an init failure only after successful
command registration. The regression uploads its real DKM, selects and restarts,
checks E_INIT and absence from loaded modules, then checks E_PROTO for its command.
It passes on DT2 1.17 with the real CF loader and zero-DSP harness (17.08 seconds
body, 29.31 seconds overall). Two coupled attempts timed out during startup,
before the test body; no DSP or audio acceptance is claimed by this CF-only test.

Runtime build/current check, six targeted gate-routing tests, Python lint/format,
TypeScript types/format and scoped documentation checks pass. The gate now packs
the fixture for this regression. Changed-file preflight still fails only the
existing global 256 stale references. Logs: out/canopy-publication-{build,check,
routing,live,changed}.log. No hardware changes or public V2 enablement.


## Follow-up: admitted playback adapter (Stage 1 still open)

Added core/instrument-play, a non-default DT2-only adapter for one admitted row
and track 1. It receives normalized MIDI/configuration into owned buffers, invokes
the protected V2 process once per block, and copies cached output through a separate
optional raw-carrier table. The stock voice adapter checks legacy registrations
first. The new table never replaces a stock type and retains silence on ownership
loss rather than falling back to a stock renderer for the added type.

Separated CANOPY's shared atomic note engine into events.s, so a future uploaded
module can omit the legacy track-bank callbacks. REPLACE reset now retains
oscillator phase/gain for the existing bounded 64-sample release ramp, clears
pending notes/pedal ownership, and waits for quiet before new configuration.
Other reset reasons retain their clear-state behavior.

Validation: 55 CANOPY callback/legacy-engine tests, 145 playback/stock-voice tests,
and two additional optional-table/legacy-priority tests pass. Build/current checks
passed for core/instrument-play, core/dsp-voice and test/canopy-transport. Logs:
out/canopy-event-split.log, out/canopy-play-host.log,
out/canopy-play-dispatch.log, out/canopy-play-build.log, out/canopy-play-check.log.
The instruction test caught a duplicate global code/data declaration for the
optional table; removing the redundant GLOBAL data declaration fixes its address.

The normal-file loader work is still being debugged separately. Its original
publication test passed, but the stronger completion/failure test found phase 9
(blocked); DKRT_INSTRUMENT_FILE now includes the last phase or coordinator failure.
The diagnostic identifies coordinator Boot invalidation (code 100), after CF
publication or the expected E_INIT. The runtime retains memory on this uncertainty.
Do not mark normal-file finalization complete until that regression passes.

Still required for Stage 1: normal-file completion/reclamation, public CANOPY
packing and a measured trusted admission policy, CF Sound/route readiness and
DSP fault feedback, then final stereo continuity/pedal/velocity/fault-retirement
acceptance through the admitted path. No hardware was changed.


## Follow-up: normal-file V2 completion and stale reply copies

The normal card loader now retains the CF image and DSP staging data until the
coordinator completes. It publishes CF registrations only after CREATE, and on
a failing CF initializer detaches before reclaiming either allocation. The
trusted firmware policy is optional; absent policy still refuses V2 loads.
This is single-instance, boot-only loading. Public V2 packing remains closed.

Both booted DT2 regression cases pass (249.53 seconds): successful publication
and link release, and E_INIT followed by acknowledged detach, memory release,
and removal of the failed initializer's service handler. See
out/canopy-normal-file-live.log. The earlier blocked result was a valid
same-session reply two blocks behind the prior reply, not a reset: guard
heartbeat 3478, incoming 3476, CF block 3482, reset_seen zero.

The adapter discards only checksummed, bound-session replies one or two blocks
behind; discarded copies cannot acknowledge work or refresh a deadline. Larger
regressions, token changes, reset notifications and stalls remain terminal.
The original deadline expires even under repeated stale copies. All 24 MIDI
host cases and the actual ColdFire ownership/dispatch test pass; logs are
out/canopy-reply-order-host.log and out/canopy-reply-order-coldfire.log.

Still open: CANOPY admission budgets and packaging, explicit keyboard route and
Sound ownership, DSP fault feedback/retirement, and integrated final stereo
acceptance. No hardware changes. Stage 1 is not complete.


## Stage 1 complete: admitted CANOPY integration

The real CANOPY DKM now packs V2 reset/process entries and loads under the
optional core/instrument-play policy. Explicit KEYBOARD setup, tracked Sound
ownership, asynchronous chooser refresh and V3 DSP fault feedback connect the
normal loader to the shared carrier. The integrated acceptance passes (347.66 s):
normal upload/readback/restart, assigned machine selection, USB/DIN C-E/add G/
release E final stereo continuity including transitions, pedal ownership,
independent velocity and callback-fault detach/reclamation. See finding 78 and
out/canopy-stage1-live.log. All Stage 1 checklist items are now checked.

The reply guard uses an eight-record exact-copy cache, not a numeric backward
window. Unknown regressions and altered ACK/status still fail closed; discarded
copies cannot refresh deadlines. 25 host cases and actual ColdFire dispatch pass.
The view refresh reuses the archived stock builder, verified against current
1.17 bytes. Full port resolution needs an unavailable CF analysis database, so
only the new window was resolved with port_resolve and the port was rendered
with its existing entries preserved. No addresses were guessed.

Keep scope clear: optional DTII-only firmware, one admitted track-1 instance,
boot-only package, nonpersistent keyboard settings, development uploader/selection
path. Generic module-browser fit remains V1-only. Hardware CPU, coexistence,
polyphonic pad sound/chord modes/arp and finished SRC UI belong to later stages.
No hardware changed. Do not extend this task into Stage 2 without new direction.


Final validation: the updated normal-file loader regression passes both cases
(publication and failed-initializer acknowledged detach/reclamation) in 259.49 s,
log out/canopy-final-loader-live.log. Build-current checks pass for runtime,
MIDI events, instrument playback and menu. Scoped changed-document pathcheck is
clean. The changed-file host suite passes 204 tests (16 skipped); lint, formatting
and layers pass. The global pathcheck retains its pre-existing 256 stale references
in 78 files. Full merge-gate/device release coverage remains separate.
