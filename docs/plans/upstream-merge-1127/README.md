# Upstream merge of 2026-11-27 (upstream `085759d37`)

The fork (`main`, `2e5085f22`, after the stock chords of #27) takes upstream `main` at `085759d37`: upstream's #431, #432, #473 and #749
to #770 since the last integration (`21f8c0a63`, [upstream-merge-1125](../upstream-merge-1125/README.md)). Branch
`integrate/upstream-1127`, tree `~/digikit-hw` on the WSL disk.

Upstream's work in the range, as far as it touches the fork:

- **The installer and the port file:** task facts and a context per hook in the port files; hooks written and restored only while the
  lower tasks are parked (#751); the DSP take-down's busy and pending cases (#752).
- **Service reload** (#754, #767): `RELOAD` of a service file and its consumers through the manager, refused for a service with an
  interrupt-entered hook or one that registers the settings overlay (`E_BOOT_ONLY`).
- **The SETTINGS page** (#753, #755, #758): the stock SETTINGS list rebuilt when a row is added after its build, a settings page
  component from a field table, and `dk_ui_choice_at`.
- **`core/lfo-table`** (#770): the LFO waves 7 to 14 for the lfo service, in upstream's own DT2 sets (`emulator`, `device`,
  `device-introblank`, `sample-tracks`).
- **Tools and tests:** the Transfer pane's live operations (#761), the glyph tool (#762), Nx input fan-out and image inputs (#759,
  #764, #765, #766), the test-support split (#769: `aggregate-error`, `reusable-session`, `test-runtime`, `module-artifact`), findings
  and docs.

## The merge

21 conflicts, all resolved in the merge commit (`153c4b47b`):

- `devices/digitakt-ii.toml`: upstream's `core/lfo-table` in its own sets (`emulator`, `device`, `device-introblank`,
  `sample-tracks`); the fork's own profiles (`relay-track`, `phrase`, `overloop`, `canopy-overloop`, `canopy-overloop-prism`,
  `canopy-overloop-prism-relay`) as they were. The fork's hardware sets carry no lfo service, so they take no LFO table. (The merge
  commit's message names only `emulator` and `sample-tracks`.)
- `runtime/core/module.toml`: the union of the optional imports (upstream's `dk_ui_choice_at`; the fork's DSP link claims and instrument
  providers).
- `tools/nx/modules.mjs`: upstream's input list for a content module's load selector, with the fork's private closure (it stops at the
  shared loader, `e43f8fcbf`).
- `services/machines/tests/state.rs`: the fork's imports (its `DKRT_MF_EXT` cases).
- `tests/regression/runtime/fx-session.ts`: upstream's split helpers, with the fork's `resolveInputs` and `sampleWav`.
- 14 generated objects, the two boot-effects snapshots and the device-literal baseline: upstream's side in the merge commit,
  regenerated in the next.

The partial clone needed upstream's new blobs first: `-c fetch.negotiationAlgorithm=noop` in the batch fetch (without it the server
sent about half of them).

## Decisions

- **D1, the system package block: 385,024 bytes.** The package needs 381,920 bytes after the merge (upstream's growth on top of the
  fork's instrument host); the block was 360,448 (upstream 327,680). It is runtime bss in cf.expansion; every DT2 device image still
  applies.
- **D2, no LFO table in the fork's hardware sets** (above).

## Regenerated

`patchkit gen` (bindings and `reference.json` already current; `dkrt.h` and `gen_abi.rs` for D1), the objects of both trees, the
boot-effects snapshots (`UPDATE_GOLDENS`: flush sizes only) and the device-literal baseline. `patchkit gen --check` and the three fork
seams' `--check` pass.

## Found and fixed by the gates

The first run of the gates found these; `67d667895` fixes them.

- **The test-support split (#769).** Upstream removed `tests/regression/support.ts` and `tests/regression/runtime/install.ts` and
  moved their exports into one file each. The fork's 33 device tests (CANOPY's and the track keyboard's, the manual ones among them)
  and its regression files (the `canopy-*`, `instrument-*`, `dsp-reset`, `init-failure` and `midi-ingress` tests, `card-seed.ts`,
  `panel-ack.ts`, `session-rt.ts`) still imported them: every one of those device suites failed to load. They import
  `session-handle.ts`, `aggregate-error.ts`, `test-runtime.ts`, `runtime/module-artifact.ts` and `runtime/module-install.ts` now.
- **`runtime/core/owners.toml`.** Both sides had added `dkrt::stock_kit`: the fork under `C:diagnostics`, upstream under `shared`.
  The merge kept both and `owners.toml` refused the prefix under two tags; it is upstream's `shared` alone now.
- **`services/machines/tests/state.rs`.** Upstream dropped the `name_hash` import, unused on its side; the fork's `ext_info` cases
  use it.
- **`services/lfo/dks/Cargo.lock`.** Upstream's lfo service 0.9.0 depends on `dkrt-abi` and `dkneeds` in its `Cargo.toml` but not in
  its lock, so `--locked` refused it. The lock has them now (upstream's lock is the one from before the merge, so its own tree is
  refused too).
- **`core/phrase-control`.** Its `test-host` inputs (the fork's own list) name `vitest.host/module-tests.json`, which upstream's
  `check-vitest-host` requires of every target that runs a host Vitest project.

## Upstream's own failures

Each is in files the merge took from upstream unchanged (byte for byte), so upstream's tree fails them for the same reason (not run
there):

- **`patchkit::devdata the_patch_sets_are_the_rt021_flash_modules`.** Upstream added `core/lfo-table` to its DT2 `device` set (#770)
  but not to the set this test expects.
- **`@digi/desktop:check:ui` and `@digi/web:check`.** Upstream moved `modulePath` and `groupOf` from `dktransfer.ts` into
  `module-path.ts` (`fc8013315`); `apps/desktop/ui/preview/module-pages.ts`, `module-store.ts` and
  `apps/web/tests/dkneeds.wasm.test.ts` still import them from `dktransfer.ts`, which only imports them now. The same import fails
  `dkneeds.wasm.test.ts` in `@digi/web:test-wasm` (`groupOf is not a function`).
- **SHOT on the DN2.** `shot-dn2-audio.device.test.ts` fails its retrigger phase with the levels of the last integration (B before
  the retrigger and A after it, both 0.026 against the floor of 0.05), which upstream's own tree failed the same way then
  ([upstream-merge-1125](../upstream-merge-1125/README.md)).

## Gates

On the host and the DT2 1.17 and DN2 1.12 emulators, on the WSL disk (2026-11-27); nothing ran on a device.

- **Build and fit.** Every project builds; `patchkit:device-images` applies every DT2 set, `canopy-overloop-prism-relay` among them.
- **Checks.** `patchkit gen --check` and the three fork seams' `--check` pass.
- **The fork's device suites** (the track keyboard's, CANOPY's arpeggiator, CANOPY, Overloop, PRISM, RELAY, REMOTE): 129 tasks
  pass, none fails.
- **Upstream's device sets** (machines, fx, lfo, settings, services, capacity, the settings page, SHOT): 157 pass; SHOT's DN2 case
  fails (above).
- **Install rehearsal.** The hardware set `canopy-overloop-prism-relay` with the owner's settings record (the backup of 2026-11-27):
  the five modules of the saved selection (CANOPY, Overloop, REMOTE, PRISM, RELAY) load with result 0 and no fault, and every
  settings record reads back the same. The arena uses 354,400 of 450,560 bytes (the largest free block 83,328; the installed image,
  `main` at `2e5085f22`, used 354,784), the DSP area 227,520 of 524,224 (the same as before), the system package 381,920 of its
  385,024 (357,424 before, D1).
- **The merge gate** (`DIGI_GATE_BASE=origin/main mise run test`): `nx: 257 ok, 9 failed`, none from the merge. The failed tasks:
  - `patchkit:rust-tests` and `gate-rust-modules:rust-tests`: `the_patch_sets_are_the_rt021_flash_modules` only (upstream's, above).
  - `@digi/web:check` and `@digi/desktop:check:ui`: upstream's `dktransfer.ts` imports (above).
  - `@digi/web:test-wasm`: upstream's `groupOf` import (above) and RELAY's missing README (the gallery wants one of every shipped
    module, as on `main`); three cases ran out of their 120 s under the gate's load. Run alone, the three pass (the byte-for-byte
    check of each device set's runtime patch against the command line among them); besides the two, only a `dkupload.wasm.test.ts`
    timing case failed there, and it passed the three runs after.
  - `gate-fx-dn2:fx-no-aot`, `gate-fx-dn2:tier-quick` and `tests/patchkit/test_pk_current.py`: the firmware function database, which
    this machine cannot rebuild.
  - `gate-fast-roots:fixtures`: the DN2 runtime fixture's wall-clock timeout, as at the last integration.
  - `tools:python-tests`, besides `test_pk_current.py`: `test_debug_module.py` and the two sample-pool tests, as at the last
    integration (they fail the same way on upstream's tree).
  - `digiemu-desktop:rust-tests`: the system library `dbus-1` is not installed.

## Follow-ups

- **Upstream's own failures.** The devdata expectation and the `dktransfer.ts` imports are upstream's to fix; the fork takes the fix
  with the next integration rather than diverging from upstream's files now.
- **Hardware.** Nothing is installed. An install builds the bundle from the merged `main` and rehearses it again before anything is
  flashed.
