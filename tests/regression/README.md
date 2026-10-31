# @digi/regression

Vitest suites that drive the emulator daemon (`emud`) through `@digi/test-support` and `@digi/control-client`.
All device suites share one config, `vitest.config.ts`, with named projects. Device projects run one file at a
time (the stock project may run files in parallel, `REGRESSION_JOBS`, set by the Nx launcher from its budget, default 2).

## Layout

| Directory | Project | Content |
| --- | --- | --- |
| `dt2/` | `stock` | Stock DT2 1.17 UI and transport goldens, zero DSP |
| `dt2/midi-ingress.test.ts` | `midi-ingress` | Experimental DT2 USB/DIN parser-to-task observation, zero DSP |
| `scenarios/` | `fx-dn2`, `fx-dt2` | Stock FX oracles (see [scenarios/README.md](scenarios/README.md)) |
| `emulator/` | device | Emulator checks that belong to no module: stock-firmware reply record, SHARC instruction semantics |
| `runtime/` | none | Helpers only (daemon, card, UI and module-install kits) |
| `runtime/canopy-bend.test.ts` | `canopy-carrier` | Final-audio bend frequency, MIDI route isolation and CC121 centering |
| `runtime/canopy-panic.test.ts` | `canopy-carrier` | Route-scoped channel-mode controls, sustain, panic and stale physical releases |
| `runtime/canopy-pattern.test.ts` | `canopy-carrier` | Running pattern lookahead, committed-boundary admission and stale-release ownership |
| `runtime/canopy-reload.test.ts` | `canopy-carrier` | Panel-driven temporary reload, same-pointer invalidation and fresh MIDI audio |
| `runtime/canopy-carrier.test.ts` | `canopy-carrier` | Emulator-only initialization, machine/pattern re-admission and final PCM without audition |
| `runtime/canopy-transport.test.ts` | `canopy-transport` | Emulator-only CANOPY MIDI/reset/configuration/event ACKs with real DSP |
| `runtime/overloop-display.test.ts` | `overloop` | Experimental DT2 Overloop display, capture and live playback window |
| `host/` | none (root `regression-host`) | Mocked helper tests, no daemon or firmware |

Device tests for a module live in that module: `modules/**/tests/*.device.test.ts` (patched runtime on DT2 1.17
and DN2 1.12). The owner's `project.json` lists them in `metadata.runtimeAcceptance`, with paths relative
to that project. A record with `dksampleWasm: true` makes the target depend on `dksample:wasm`, which builds
`apps/web/public/dksample.wasm`. A record with `pythonTools: true` hashes the Python tools in `PYTHON_TOOL_SOURCES`, and `pythonTools: "client"` only those in `PYTHON_CLIENT_SOURCES` (`tools/nx/module-targets.mjs`), for tests that spawn Python tools. Runtime acceptance belongs to `modules/core/runtime/tests/project.json`
(`core/runtime-tests`) except CF-only service commands and settings, owned by nested `service-commands/project.json`
and `settings/project.json` (`core/runtime-service-tests` and `core/runtime-settings-tests`). Run an owner's
`test-device` directly; `module:check core/runtime` discovers all owners. Checks that run over
every shipped module live in `tests/conformance/`; its load cells (`load-dt2.device.test.ts`, `load-dn2.device.test.ts`) upload each run-time
module and checks persistence, contract, registration and faults. `DIGI_CONFORMANCE_MODULES` (comma-separated
module ids) limits it; unset runs all.

Shared helpers: `support.ts`, `timing.ts`, `runtime/devices.ts`, `runtime/ready.ts`, `runtime/waits.ts`, `runtime/install.ts`,
`runtime/artifacts.ts`, `runtime/protocol.ts`, `runtime/fixtures.ts`, `runtime/remote.ts`, `runtime/runtime-plan.ts`,
`runtime/runtime-menu.ts`, `runtime/transfer-cli.ts`, `runtime/probe-kit.ts`. Module tests import them as
`@digi/regression/<path>` (for example `@digi/regression/runtime/ready.ts`).
Goldens are in `goldens/dt2-1.17.json`.

## Commands

| Command | Project | Notes |
| --- | --- | --- |
| `mise run test:stock [-- FILTER]` | `stock` | `DIGI_EMUD` names a current DT2 daemon |
| `REGRESSION_JOBS=1 pnpm --filter @digi/regression test:update` | `stock` | Rewrites goldens |
| `pnpm nx:pilot run <module-id>:test-device` | `runtime` | Builds the packs and plusdrive first (Nx targets, into `dist/`) |
| `pnpm nx:pilot run-many -t test-conformance` | `conformance` | Builds the packs first |
| `mise run test:fx` | `fx-dn2` | 61 quick; `DIGI_FX_SWEEP=1` for 191 |
| `mise run test:fx:dt2` | `fx-dt2` | 135 quick; `DIGI_FX_SWEEP=1` for 146 |
| `pnpm test:host` | host projects | No device inputs |

Filters are native Vitest file or test-name filters. `mise run test:stock`, `test:fx` and `test:fx:dt2` run through Nx, which builds the prerequisites and reuses cached results.
Running `pnpm --filter @digi/regression test:*` or Vitest directly builds nothing, and missing prerequisites fail. `REGRESSION_JUNIT=1` writes `out/regression/junit.xml`.

## Prerequisites

- Pinned daemons: `DIGI_EMUD` (stock), `DIGI_EMUD_DT2` and `DIGI_EMUD_DN2` (runtime, modules, FX).
- Ready inputs for the device, validated by `resolveInputs` (CF snapshot, DSP image and state). Runtime inputs
  must match the stock SHA-256 and `intc_force:false` in the ready directory's `SOURCE.json`.
- Module artifacts: `dist/modules/<device>/<leaf>.dkm` (override the root with
  `DIGI_RUNTIME_MODULES`). Runtime cases need `input-probe`, `remote`, `svc-echo` and `debug` (the log: every cell that reads it
  selects `digikit/debug` first, `installWithDebug` in `runtime/install.ts`); the load cells need every run-time module. The gate packs them itself. By hand:
  `pnpm nx:pilot run digikit/pluck:pack` (artifacts in `dist/modules/<dev>/<leaf>.dkm`).
- DT2 card samples at `out/runs/patchkit/samples` (`tools.emulator.card_check.make_samples`).
- Sample cases also need `apps/web/public/dksample.wasm`. The Nx target `dksample:wasm` builds it for records with
  `dksampleWasm: true`; by hand use `mise run wasm:dksample`.
- plusdrive is built by the Nx target `plusdrive-format:cli` into `dist/plusdrive/plusdrive` (`DIGI_PLUSDRIVE` overrides the path).
  There is no `DIGI_RUNTIME_CARD` variable.

## Stock suite

The separate `midi-ingress` project requires a private observer artifact. Prepare
it with `uv run python -m tools.coldfire.midi_ingress_probe`, then run
`DIGI_EMUD=PATH_TO_DAEMON pnpm --filter @digi/regression exec vitest run --project midi-ingress`.
The observer and displaced firmware bytes stay under `out/`. The test observes
the original gathered-message task hooks, checks receive-enable and auto-channel
behavior, and continues through stock routing. It uses the stock ready snapshot
with zero DSP; it proves neither CANOPY ownership nor audio. It is excluded from
the ordinary stock golden suite and never builds its artifact implicitly.

Each test checks no guest fault, DSP halt or stall (`Session.expectNoFault`), and that the stock firmware writes
nothing into the patchkit free regions: `guard.set` is armed with the `cpu = "cf"` `[[region]]` blocks of
`devices/ports/<device>-<fw>.toml` (`cf.flash_cave`, `cf.expansion`, minus `excludes`) with an empty
`allow_pc`, and `guard.report` must total 0.

Frame (SHA-256) and LED goldens: `dt2/rec-play` (scenario `dt2-rec-play` of `tests/scenarios/dt2.json`, exact
instruction counts, 6 s), `dt2/pages` (SRC, FLTR, AMP, FX, MOD), `dt2/transport` (PLAY, STOP). The tests run
with `--dsp zero`, so nothing checks sound or DSP faults (`docs/findings/07-emulator.md`, "Zero DSP replies for
UI scenarios"). A golden changes only with a reason; review the diff.

## Runtime workflows

Module device tests: `core/runtime` (`service-commands`, `transfer-live`, `samples-live` on DT2,
`samples-store-live` on DN2, `modules-capacity`, upload, settings and crash tests), `core/input`
(`input-adapter`), `modules/tools/remote` (`remote-input`), `modules/tools/debug` and `modules/tools/probe`. Those workflows use a
fresh runtime daemon for each device case.

FX route acceptance on both devices lives in `modules/core/adapters/fx/tests/fx-routes-panel.device.test.ts`
(route choices, defaults, capacity and pending changes), `modules/core/adapters/fx/tests/fx-routes-persist.device.test.ts`
(apply/restart persistence and PCM), and `modules/core/adapters/fxpages/tests/fx-track-route.device.test.ts`
(track SEND pages, amounts, kit loads and project-byte controls). These replace the Python `fxroute` and
`fxroutes` scenarios. Independent cases reload clean card baselines in an owned daemon; apply/restart cases
preserve the guest's writes. Track SEND's image without the page hook closes before its image with the hook
starts. Offline profile, card settings, OLED and project-byte helpers remain in
`tools/patchkit/pk_digikit_fxroute.py` and `tools/patchkit/pk_digikit_fxroutes.py`.

The gate prepares the complete card inventories, including renamed effect packs; manual runs need those
prepared artifacts, the plusdrive builder and the native patchkit CLI used by store dump.

`emulator/reply-inflight.test.ts` runs stock firmware with the reply-record check. `emulator/sharc-semantics.test.ts`
runs the SHARC probe modules and compares RECIPS, RSQRTS and the dsp-facts values with the device measurements
in `tests/fixtures/sharc/`.

Helpers in `runtime/devices.ts`, `ready.ts`, `waits.ts`, `install.ts`, `artifacts.ts`, `protocol.ts`:

- `runtimeTests(name, body, opts)`: one test per device, after `expectRuntimeBoot`.
- `liveOptions(device, extra)`: daemon binary, firmware version and card for `withPatchedSession`.
- `expectRuntimeReady`: wait for the runtime's normal UI hook on a fresh free-running daemon.
- `waitEmulated`: wait for emulated time (DSP frames); wall time is only a hang guard.
- `installModule`, `moduleArtifact`: install a prepared `.dkm` and locate it.
- `tap`, `record`: panel key presses and a log line in the run artifacts.
- `liveArtifacts`: the run artifact directory.

Helpers in `support.ts`:

- `withSession`: one stock daemon per file, restarted before each test.
- `withPatchedSession`, `startPatchedSession`: coupled daemon with `firmware.load {runtime:true,
  start:"auto"}` and the `emulator` patch set.
- `expectRuntimeBoot`: checks patch identity, ready/free status and health.
- `freeRegions`, `armFreeRegionGuard`, `expectNoGuardHits`: the free-region guard.

Runtime diagnostics emit one JSON line per completed phase with `event: "runtime-phase"`, a device,
scenario scope and unique session ID (including startup hooks), phase, outcome and monotonic `durationMs`.
`session.startup` includes input/card/firmware preparation; `boot.validation` measures the subsequent checks.
`scenario.body` includes its nested settle, upload/install, restart and boot-wait phases. CLI install with
restart is measured as one aggregate operation. Each record has a unique `span` and optional `parentSpan`;
all durations are inclusive, so do not sum a body or install span with its children. Shutdown records share
the session ID even when they run outside the test body. Failed operations also emit a record.

Stock restart and instruction-step waits are rejected for runtime sessions. Cleanup cancels protocol requests
and closes the owned daemon.

`transfer-live` runs the `tools.device.dktransfer` CLI (`runtime/transfer-cli.ts`) for install, delete and
protocol restart, then checks the store, the mirror hash, `card.folder` metadata and the next boot.

`input-adapter` and `remote-input` check semantic KEY navigation, TURN delivery and decoder acceptance, not
vendor parameter changes. Exact remote pacing is checked by the host C unit
`modules/tools/remote/tests/pacing.test.ts` (it compiles `remote-pacing.c` with `cc`).

## Module load cells

`tests/conformance/load-{dt2,dn2}.device.test.ts` upload every shipped run-time module (dynamic, service and host
machine or effect modules) on DT2 1.17 and DN2 1.12 through `DkSession` and `EmuUsbTransport`, selects it,
reboots, and checks persistence, SDK contract, DSP-loader completion, registration, guard counters, runtime
log and guest faults. Each cell has a fresh daemon and reuses the worker's cached run dir. These cells observe load and registration, not rendered
audio. Module-specific behaviour is tested in the module's own `tests/` folder.

```sh
pnpm nx:pilot run digikit/remote:test-conformance
```

## Artifacts

Live workflows write to `<os tmpdir>/digi-regression/<workflow>/<device>/run-*`. Within a Session the directory is
reused until the test that requested it finishes. It is removed when that test passes and kept on failure (the
run prints `artifacts kept: <dir>`).
FX runs write reports and logs to `out/regression/{trackfx,sendfx,dt2-fx}/<timestamp>-<pid>/`. When every test of
the file passed, the run directory is removed; on a failure the reports and logs stay (WAV files are deleted).
`DIGI_FX_KEEP_RUNS=1` keeps the whole run directory including WAV files.
Module cells write diagnostic JSON under `out/regression/module-load/<device>/<module>/`.

## Instrument session bootstrap proof

This isolated DTII project uses the ready inputs and existing daemon above. It
cold-boots the registry and triggers the runtime's one-shot bootstrap, checks the
DSP session token and drained link release, and rejects rearming. A second image
injects token loss and then restores the old token/ACK; the CF runtime must remain
blocked without another transfer. No machine is admitted. The optional trigger
and fault injector are supplied only by the test fixtures.

```sh
uv run python -m tools.patchkit.patchkit build core/runtime test/instrument-session test/instrument-session-loss
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --modules test/instrument-session -o out/patch/canopy-live-session.syx
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --modules test/instrument-session-loss -o out/patch/canopy-live-session-loss.syx
DIGI_EMUD_DT2=PATH_TO_DAEMON pnpm --filter @digi/regression exec vitest run --project instrument-session
```

These tests prove the running firmware's initial exchange and sticky revocation
after injected token loss. They do not emulate a DSP-only hardware reset or prove
that its reset paths clear the latch. Do not install these fixtures on a device.

## CANOPY transport proof

This isolated project requires the existing DT2 daemon and ready inputs above.
Prepare the explicit test fixture; it never builds implicitly:

```sh
uv run python -m tools.patchkit.patchkit build test/canopy-transport
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --modules test/canopy-transport -o out/patch/canopy-live-transport.syx --plan out/patch/canopy-live-transport.plan.json
DIGI_EMUD_DT2=PATH_TO_DAEMON pnpm --filter @digi/regression exec vitest run --project canopy-transport
```

The test verifies the artifact hash against its plan, cold-boots with real DSP,
waits for actual reset/configuration ACKs, then checks USB/DIN note tokens,
independent releases and acknowledged queue drainage. It changes receive settings
only in its disposable emulator. The fixture conflicts with `core/loop-controls`
because it uses the optional controls tick for bootstrap. No renderer or public
machine descriptor is installed. It is excluded from the normal runtime suite.

This image is emulator-only: current firmware preflight reports two tuning-table
data values as instructions. Keep the preflight failure visible; do not flash or
bypass it. Details are in the [carrier findings](../../docs/findings/78-canopy-carrier.md).

## CANOPY carrier proof

This separate emulator fixture requires the same DT2 daemon and stock ready inputs
as the transport proof. It deliberately replaces stock WERP's raw renderer and
must not be flashed. Prepare explicitly:

```sh
uv run python -m tools.patchkit.patchkit build test/canopy-carrier
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --modules test/canopy-carrier -o out/patch/canopy-live-carrier.syx --plan out/patch/canopy-live-carrier.plan.json
DIGI_EMUD_DT2=PATH_TO_DAEMON pnpm --filter @digi/regression exec vitest run --project canopy-carrier
```

The test cold-boots without a sample card and waits for reset acknowledgement
with MIDI routes closed. Selecting WERP with the panel controls lets the fixture
resolve the live Sound and refresh its stock rows, then obtain configuration
acknowledgement and enable routing. It sends no stock audition or sequencer trig.
USB/DIN keys exercise independent rendering and full release
tails. Final PCM must contain finite nonzero samples while G remains held through
E's release, and the final key release must retire the carrier. Idle and
post-release PCM windows must each contain at least 4,096 scalar samples with
finite RMS and peak below -120 dBFS; the stock output is not required to be
bit-exact zero. The test also saves the stock cached source pointer, source type,
outgoing row type and DSP mirror after initial admission, checking one refresh.
It then leaves and reselects WERP with a held key, proves an old same-pitch release
cannot kill a fresh note, and changes to a stopped destination pattern. Routes
remain closed until WERP is selected there; a fresh note then renders and retires.
The scenario checks three refreshes, two invalidations and generations 2 through 4.
It then wakes the carrier with velocity-24 C before adding velocity-127 G,
measures their mixed output, releases C and measures G alone, and compares that
G with the same pitch/velocity started from silence. Held windows settle for
0.25 seconds before measuring at least 24,000 scalar samples. The G-alone RMS
ratio must be within 2%; the mixed ratio must be 0.95â€“1.10 to allow low C's energy
and finite-window beating. These bounds detect shared gain inherited from the
soft first key; they do not prove phase continuity or all velocity curves.
The extended carrier test has an eight-minute body ceiling.
Reset quietness checks allow 24,000 scalar stereo samples (0.25 seconds) to settle
before measuring a separate window against the unchanged -120 dBFS threshold.
The companion `runtime/canopy-reload.test.ts` uses FUNC+YES then FUNC+NO while
holding a MIDI C. It checks that the load hook fires, the same Sound pointer
survives, the generation advances, the old voice retires, and preparation completes
again. A new C survives the old C release; C/G then produce finite PCM above -80
dBFS RMS. Their release retires both voices, followed by a quarter-second drain
and a separate final-output window below -120 dBFS RMS and peak. It then holds a physically released USB C under CC64 while a DIN G releases
normally, measures C alone after 1.25 seconds (longer than its local release),
and verifies pedal-up releases C while a fresh physically held G stays active.
Final G release must restore output below -120 dBFS RMS and peak. This test has a
five-minute body ceiling and can run alone by appending
`runtime/canopy-reload.test.ts` to the command above.

The `runtime/canopy-pattern.test.ts` companion prepares WERP on two patterns,
starts playback, holds C and queues a compatible switch. It requires observed
scheduling lookahead, unchanged ownership through destination commitment, release
by the original key-up, and fresh C/G playback afterwards. Its body ceiling is
five minutes. Settled release output must fall below -120 dBFS RMS and peak.

Stereo f32le capture includes at least 8,192 settled source frames before the
switch is requested and continues through settled destination audio. Alongside
window energy and adjacent-frame bounds, `sine-continuity.ts` estimates frequency,
amplitude and phase from that source prefix alone. It predicts every subsequent
sample without refitting. Each channel must stay within 0.1% of fitted amplitude,
with a second-order sine recurrence residual below 0.001%. This is a bound for
the fixed C4 sine and default Sound, not arbitrary musical audio. Host fault
injections prove detection of a dropped sample, duplicate sample, oscillator
restart, gain change and short gap on the right channel independently.

Captures, ownership traces and the continuity report remain under
`out/canopy-pattern-*`. UI polling is not sample-aligned; prediction covers the
entire audio interval rather than assigning a PCM index to the UI transition.
Other clock modes, pause/resume, rapid or cancelled switches, other Sound controls
and other UI load paths are not covered. The separate
`tests/patchkit/test_canopy_sound_load.py` component proof runs the real stock
deserializer with synthetic records, including unrelated and rejected loads.
This remains emulator acceptance; the carrier fixture is not a hardware release.
See the [carrier findings](../../docs/findings/78-canopy-carrier.md).

The `canopy-carrier` project's `runtime/canopy-panic.test.ts` has a four-minute
body ceiling. It sends USB CC123 under sustain, then USB CC120 while DIN holds a
note, and verifies that fresh same-pitch USB notes survive old physical releases.
After panic clears sustain, CC123 must release the fresh USB notes normally.
The unaffected DIN note must remain audible (finite RMS above -80 dBFS); DIN panic
then requires final RMS and peak below -120 dBFS after the usual 0.25-second drain.
The instance generation must remain unchanged: this tests route-owned controls,
not a whole-track reset. Artifacts use the `out/canopy-panic-*` prefix.

The `canopy-carrier` project's `runtime/canopy-bend.test.ts` measures final pitch
from raw 48 kHz stereo captures of held USB A4. It checks center, maximum upward
bend, an unrelated DIN downward bend, and USB CC121 centering. Each capture
follows a quarter-second settle and covers at least a quarter-second of audio.
Interpolated positive zero crossings on the left channel must yield pitch within
0.2 Hz of the target; output must remain finite and audible. The four-minute
scenario also checks the same note token survives throughout and retires on
physical release. Artifacts use `out/canopy-bend-*`. This measures settled final
pitch; sample-accurate phase preservation is covered by component tests.
