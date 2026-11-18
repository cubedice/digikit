# Upstream 870a7319 merged into the fork: service files, bind-time firmware names, RS-C, RS-S

DT2 OS 1.17 and DN2 OS 1.12, WSL emulator, 2026-11-18. It follows
[HANDOVER-2026-11-16-upstream-merge.md](HANDOVER-2026-11-16-upstream-merge.md), whose section "Upstream 257a3770: tried, not
merged" listed what this merge had to port. Everything below is from this session's reads, builds and runs unless it says
otherwise. Nothing was installed on hardware.

Branch `integrate/upstream-b1-rsc`, cut from fork `main` d6c67892 (PRISM as a tape delay is in it). Local, not pushed, no pull
request. Upstream `main` was 870a7319 (411 commits past the last merged point 839a43e1). Fork `main` 09f86c61 was merged
in afterwards.

| Commit | What |
| --- | --- |
| 49e875cd | The merge: 29 conflicts resolved, the fork's code in moved code ported, generated sources and objects rebuilt. |
| 06a7eb5c | CANOPY's needs test counts the arena of the machines service. |
| 6980c469 | Fixes after the first gate (below), and the repeated measurement in finding 353. |
| f7377998 | `core/runtime:test-assembly` depends on the bind of the lfo service; this handover. |
| 1de4b9fa | Merge of fork `main` 09f86c61 (the device bug fixes), see the next section. |

## Fork `main` 09f86c61 merged in (2026-11-18)

`main` gained three fixes after the branch point (pull request 5 of the fork): the limit of PRISM's tape feedback
(a5f37017), the recovery of CANOPY's faulted note transport with status frames on one block in three (66436940) and the
working copy of each track's CANOPY settings (ef879683); findings 354 (addition), 355 and 356. None of them touches code
that the upstream merge moved, so the sources merged by themselves. Two conflicts:

- `tests/regression/runtime/write-safety.ts`: the policies `settings` and `fx-routes` allow all 16 sectors of each
  settings copy (from `main`: the settings record with the working copy is longer than a sector); a card with the
  machines service allows the first sector of each copy under any policy (from upstream). Both host expectations are in
  `tests/regression/host/write-safety.test.ts`.
- `modules/core/adapters/instrument-play/build/play-cf.dko.json`: regenerated (`mise run module:build`).
  `midi-events/build/midi-packet-dsp.dko.json` came out of the regeneration as on `main`.

Run on 1de4b9fa, all passing: `regression:test-host` (with `canopy-link-recover`, `canopy-work-copy` and
`write-safety`) and `digikit/prism:test-host` (the engine tests); `tests/patchkit/test_midi_events.py` (99 passed, 3
skipped); the nine device tests of CANOPY and PRISM on the DT2, each run and none replayed; the static checks,
`mise run check:layers`, `patchkit:objects-check` and `core/runtime:test-assembly`. `gate-smoke:smoke` and
`gate-pins:pins` were replayed from the cache: their inputs did not change with this merge, and their last run was the
full gate on 06a7eb5c. The full gate was not run again.

Fit on the DT2 at 1de4b9fa: `canopy-overloop-prism` uses 1 317 300 of 1 396 736 bytes of `cf.expansion` (79 436 free;
1712 bytes more than before this merge), `canopy-overloop` 1 292 312 (104 424 free). On `main` 09f86c61, with the
arena before upstream's services, the first figure was 1 390 336 (as reported by the session that made the fixes; not
measured here).

The interrupt stack measurement (finding 353) was not repeated after this merge. The new routines `recover_tick` and
`work_tick` are called from instrument-play's tick on the UI task (comment in `control.c`), not from the timer task
whose stack the finding measures; that was read, not measured.

## What upstream changed under the fork

- The machine and effect hooks left the image adapters for service files the runtime loads from the card
  (`services/machines.dks`, `fx.dks`, `samples.dks`; `lfo.dks` was there). A module with a machine or an effect has a hard
  need on its service; a card without the bound file refuses the module (`E_MISSING`, -73).
- B1: a service file holds no firmware address. It names firmware by `dk_fw_`, `dk_exit_`, `dk_tail_`, `dk_stock_` and
  `dk_ret_` imports, and `patchkit bind` resolves them for the user's firmware.
- RS-C: the core names no machine or effect (`check:layers` rule `kind-free`); `assign.rs`, `native.rs`, `params.rs` and `fw.rs`
  of the runtime crate are gone, the lifecycle hooks `module_begin`, `module_commit` and `settle` exist.
- RS-S: the storage code is `storage/` in the runtime crate.
- The emulator hosts load each generated DSP core from a library (`sharc-core-lib`), kept in a store.

## What was ported

- **[C] The exits.** The brief said every hook routine of the fork must use `dk_tail`, `dk_exit` and `dk_ret`. Those names
  are for service files only: upstream's own image adapters still leave through patch-time `hook.*.tail` and `.return`
  (`core/dsp-link`, `core/health`, `core/params`, the image part of `core/machines`). The fork's image adapters
  (`core/instrument-play`, `pattern-position`, `phrase-ui`, `loop-display`, `midi-events`) are unchanged and build.
- **`mf_typearg` and `mf_srcpage`** are routines of `machines.dks` (`modules/core/services/machines/dks/src/hooks-dt2.s`).
  A service has no link to the image, so `core/machines` publishes the info table `machines_ext_info` (`mf_ext_info` in
  `src/cf/dt2/machines.s`: `mf_src_page_count`, `mf_src_apply_pages` and the optional `instrument_mod_param`), and the
  service copies it into `MF_EXT` at `setup` (`dks/src/image.rs`). An image without the table leaves the words 0 and keeps
  upstream's one-page rule and parameter mapping. The table is the fork's own, so upstream's `machines_info` is untouched.
- **`fxp_graph`** is a routine of `fx.dks`. `core/loop-display` publishes the info entry `loop_ui_paint` (the function
  itself), `setup` puts it in `DKRT_FXP_PAINT`, and a page it painted leaves at `cf.page.site.filter_graph_test + 0x4c4`
  (the extern `fx_graph_site`, a `dk_fw_` name with an offset).
- **Phrase.** `modules/core/services/fx/src/phrase.rs` finds `phrase_bind`, `phrase_control_tick` and `phrase_ui_tick` as
  info entries of `core/phrase-control` and `core/phrase-ui` (`phrase::setup`, called from the service's `setup`).
  `tests/phrase_stubs.rs` is gone. The calls in `records.rs`, `fxpage.rs` and `dsp/mod.rs` merged without change.
- **A persistent instrument's type numbers.** The plan of the numbers is the machines service's now. The core asks through
  two new generic `Service` methods, `planned_ids(selection, out)` and `id_taken(id)` (`dkrt-abi` `service.rs`; the
  machines service answers from `assign::planned` and `records::type_is_free`); `instrument::resolve_types` replaces
  `assign::resolve_instruments`. The service ABI hash changed (`gen_svc_abi.rs`), so every service file is rebuilt.
- **The loader fix survives.** `link.rs` still refuses a legacy DSP transfer in `Drain` only when no legacy load is in
  progress or waiting (upstream did not touch the file).
- **Arena.** `arena_bytes` is upstream's 524 288 again on both devices. With the services out of the image the hardware
  set `canopy-overloop-prism` uses 1 315 588 of 1 396 736 bytes of `cf.expansion` at 524 288 and leaves 81 148 [D] (the
  image of 2026-11-17 left 9040 at 491 520). `stage_bytes` of the DT2 stays 151 552 (CANOPY's file).
- **The cache-line rule** of patchkit merged without conflict; the expansion loader still takes 76 bytes of the flash cave
  (`test_patchkit_expansion.py`).
- **Tests.** `tests/regression/runtime/card-seed.ts` puts the bound services on a card: `seedModules` for a card seed
  folder (twenty CANOPY files used the same three lines), `putServices` for a card written over the protocol (five manual
  proofs). The records of the fork's Nx projects list `core/services/machines` and `core/services/fx`, say `dsp: sharc`,
  and carry `A:core/dynfx` and `A:core/fxpages` where they had `S:fx` (upstream closed that tag). The effect sessions
  (`withPreparedFxSession`) put the services on the card themselves.
- **By hand.** `devices/ports/dt2-1.17.toml` is merged by hand (the fork's anchors kept, upstream's removed clone sites
  dropped). `mise run patch:port` cannot run on this machine: the DT2 database here has no function with the hash of
  `cf.page.fn.source_turn`, and there is no DN2 database. Every patch set applies and every service binds, which reads
  the file; a regeneration on a machine with both databases is open.

## Proofs

- `mise run check:layers`, `patchkit:objects-check` and the static checks
  (`lint,fmt-check,rust-fmt,typecheck,python-checks`) pass on the branch head.
- Every DT2 patch set applies (`canopy-overloop-prism`, `canopy-overloop`, `phrase`, `overloop`, `emulator`, `device`).
- The nine registered device tests of the fork pass on the DT2: CANOPY `arp`, `needs`, `persistence`, `copy-handoff`,
  `pattern-copy`, `sound-transfer`; PRISM `ui`, `capture` and `coexistence` (CANOPY, Overloop, PRISM and remote on one
  image, with `machines.dks` and `fx.dks` on the card). Overloop has no registered device test of its own; the
  coexistence test is its proof.
- Host tests of the ported hooks: `test_pk_machines_fw.py` (46 cases, with the fork's SRC page cases against
  `mf_srcpage` of the service file) and `test_instrument_modulation_cf.py` (33 cases, `mf_typearg` with
  `instrument_mod_param`).
- **The interrupt stack (finding 353), repeated.** [D] During the coexistence test on the merged image the deepest store
  on the timer task's 2048 bytes is 816 bytes (1232 free); it was 888 before the merge. See the finding for the table and
  for how the measurement had to change: the daemon does not start its watch after it restored a state from the snapshot
  cache, so the run used a daemon with one added line, not committed.

## The gate

`mise run test` (base `main`) on 06a7eb5c: 95 minutes, 469 tasks, 16 failed.

Caused by the merge, fixed in 6980c469, each run again and passing:

- `core/services/fx:rust-tests`, `core/services/machines:rust-tests`, `core/services/samples:rust-tests`: the lock files
  of the service crates did not name `dkneeds` (the fork's SDK crate depends on it) and the targets pass `--locked`.
- `tools:python-tests`, three files: `test_instrument_modulation_cf.py` (the fork's test of `mf_typearg`, which is a
  routine of the service file now) and `test_pk_machines_fw.py`, `test_pk_machines_dn2_fw.py` (the unit has no definition
  of the native page calls that `machines_info` names; upstream's own files have the same gap, see the last section).

Upstream's own, with the same failure on upstream's tree 870a7319 where it says "run there":

- `core/runtime-tests:test-device--memaudit` on both devices: the audit counts the stores of the machines service into
  its own arena block and into the registry rows as stray (4664 stores at load and 71 guard hits on the DT2; run there on
  the DT2 with the same numbers but for the addresses). `dk.modules()` does not list a service, so its block is neither
  cut from the protected arena nor an allowed writer.
- `digikit/debug:test-device--dt2`: the log level of module record 0 reads 3 where the test expects 255 (run there).
- `tools:python-tests`: `test_debug_module.py` (`init 0 0` against `init 0 1`) and `test_menu_dt2.py` (`MOD_LOAD result
  -73`: the Python scenario puts no service on the card), both run there; `test_patchkit_dkrt.py`
  `test_objects_are_current_and_plain_coldfire` expects `cf.32` relocations in `tramp.dko.json`, which has none since the
  page trampolines left it (the test and the object are upstream's; not run there).
- `core/runtime-lfo-tests:test-device` on both devices: no bound `lfo.dks` in a clean `dist`. A test-only project does not
  depend on the bind of the services its records list (`tools/nx/device-tests.mjs`). Fixed here in 6980c469, the DT2 half
  run again and passing; the DN2 half was not run again.
- `native-page-proof-tests:test-device` on both devices: the fixture's test installs without the device, so the machines
  service is not on the card and the module is not listed. Fixed here in 6980c469, the DT2 half run again and passing; the
  DN2 half was not run again.

As before the merge, from this machine: `digiemu-desktop:rust-tests` (no libdbus headers), `gate-fast-roots:fixtures`
(the DN2 runtime fixture ends in a wall-clock timeout), `gate-fx-dn2:fx-no-aot` and `gate-fx-dn2:tier-quick` (argument
list too long), and in `tools:python-tests` `test_snapread.py`, `test_pk_current.py` (the port resolver needs this
machine's databases) and the two sample-pool tests (order of evaluation with this `cc`).

Under load only: `@digi/web:test-wasm` (two tests over their 120 s limit) and `tests/emulator/test_bootstrap.py`
(passes alone). Neither was run again as a task.

After the fixes the full gate was not repeated. Run again on the head: the static checks, the layer check,
`patchkit:objects-check`, `core/runtime:test-assembly` and `nx-tools:test-host`.

## What R12-8 will need (upstream branch `r12-8/slice`, plan `docs/plans/r12-8-sharc-stubs.md`)

Read from the plan and upstream's handover; not tried.

- `core/dsp-link` loses 64 bytes of its area to a block of 16 slot words (window 0). The fork's 64-word phrase suffix and
  its instrument and loop status blocks are in the same file (`src/dsp/dsp-link.inc`, `dt2.s`): a conflict in layout, to
  be fitted again and measured.
- `core/dsp-voice` is split into image stubs and a dispatch that moves into the DSP section of `machines.dks`. The fork
  changed `dsp-voice.s` (42 lines) for the instrument voices: that change has to go to the right side of the split.
- The loader gets a provider DSP load, an `Installer` trait with two backends, one unload lifecycle for modules and
  services and a late teardown. The V2 instrument path (`admit_instruments`, `queue_instruments`, `publish_v2`) and the
  `Drain` rule of `link.rs` meet it; the `Service` trait gets new methods beside `planned_ids` and `id_taken`.
- After R12-8 a service may have a DSP section, which is what kept `core/instrument-play`, `loop-storage`,
  `pattern-position` and `loop-display` from becoming service files, and the lifecycle hooks the instrument code would
  need as a service crate are on `main` now. Neither move is started.

## Not proven, open

- [O] Hardware: nothing of this branch ran on a device. An install now needs the bound `services/machines.dks` and
  `services/fx.dks` on the card beside the four modules, bound to the device's own firmware (`patchkit bind`; `wb transfer`
  uploads them with the modules). The install record of 2026-11-17 (`firmware/hardware/2026-11-17-r12/hw_install.py`,
  not in git) predates this and uploads no service.
- [O] The DN2: the fork's adapters are DT2 only and the new info entries are declared for the DT2. The DN2 halves of the
  gate ran; no DN2 test loads a fork module.
- [O] `fxp_graph` uses d1, a0 and a1 at a site whose declaration lists only `ccr` as clobbered (as the fork's routine did
  in the image adapter). It works in the coexistence test; the register use at the site was not read again.
- [O] The fork's nineteen manual proofs (`*.manual.device.test.ts`, the CANOPY proofs under `tests/regression/runtime`)
  got the service upload by one scripted edit and type-check; none was run.
- The device bug fixes of `main` (09f86c61) are merged (section above). [O] The stack measurement of finding 353 was not
  repeated with them.
- Machine facts for the next session: the Rust-for-ColdFire toolchain is at `~/.cache/digikit/canopy-codegen-gcc`
  (`DIGIKIT_CODEGEN_DIR`); the core library store must be on the WSL disk
  (`DIGI_SHARC_CORE_STORE=$HOME/.cache/digikit/sharc-core-cache`), because its insert by rename fails in the git directory
  on the Windows drive.

## For upstream (general, not the fork's)

Each is in upstream's tree at 870a7319; the first three are fixed on this branch and could go upstream as they are.

- A test-only Nx project gets no `bind` dependency for the services of its records (`testOnlyNodes` passes no
  `bindable`); `core/runtime:test-assembly` likewise reads `dist/svc-bound/*/lfo.dks` when it is there but does not depend
  on `core/services/lfo:bind`, so a bound file left by another tree fails it ("provider services/lfo.dks did not load").
- `tests/fixtures/modules/native-page/tests/native-page.device.test.ts` installs without `{ device }`.
- `test_pk_machines_fw.py` and `test_pk_machines_dn2_fw.py` do not define `mf_native_register`, `mf_native_register_v2`,
  `mf_native_remove` and `mf_native_turn` for the unit ("unresolved extern symbol").
- The memory audit does not know service blocks (above); `test_debug_module.py`, `test_menu_dt2.py`, the debug module's
  DT2 device test and `test_objects_are_current_and_plain_coldfire` are red.
- The daemon does not start its watch after a restore from the snapshot cache inside a firmware load
  (`packages/emulator/control/src/controller.rs`: `install_patched_audio` and the cache hit of the ColdFire-only load
  restore a state and do not call `resume_watch`). A watched session that starts that way checks no store: its summary
  has `cacr_writes` 0 and passes (finding 353, the repeated measurement).
- The core library store inserts by renaming a directory in the git directory; on a git directory on a Windows drive
  under WSL the rename is refused. `DIGI_SHARC_CORE_STORE` on the Linux disk avoids it.
