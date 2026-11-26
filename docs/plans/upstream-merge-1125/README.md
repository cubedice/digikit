# Upstream merge of 2026-11-25 (upstream `21f8c0a63`)

The fork (`main`, `11007c3cd`, CANOPY's voice host) takes upstream `main` at `21f8c0a63`: RC4 (the installer's `[[patch]]`,
`[[hook]]` and `[[fill]]`, `core/rows`, the machines and FX pages in their service files, the view capture in `core/ui`), RC5a
(`image-modules/` gone, `Provider::start_in`, the tick step table, registry windows, the de-naming rule) and RC5b (device tests
selected by ownership). Branch `integrate/upstream-1125`, tree `~/digikit-hw` on the WSL disk. Nothing is pushed or installed.

The merge commit (`eafb5f3bd`) resolves every textual conflict; it does not build. The commits after it are steps 2 to 5 of the
plan (the move, the port, the regeneration and the test metadata), the merge of the fork's `main` at `435a43b97` (the step entry
audition), and step 7: the device gates, the install rehearsal and the merge gate, with what they found fixed.

## The layout

Upstream's RC5a put every flashed module in `runtime/platform` and removed `image-modules/`. The fork's modules follow:

| Fork | Now |
| --- | --- |
| `image-modules/adapters/{copy-watch, fxview, instrument-play, instrument-registry, keyboard-panel, loop-control, loop-display, loop-storage, midi-events, pattern-position, phrase-control, phrase-ui, relay-input, relay-storage, relay-track, track-keyboard}` | `runtime/platform/NAME` |
| `image-modules/points/machines/src/cf/dt2/src-pages.{c,h}` (the machines image point is gone: RC4 moved it into `machines.dks`) | `runtime/platform/instrument-play/src/cf/` |

`dirmap.json` beside this file is the map; paths in the fork's files were rewritten with it.

## Decisions

- **D1, the adapters in `runtime/platform`.** `tools/nx/modules.mjs` took everything under `runtime/` for the shared loader that
  core/runtime-fixture-tests covers. It names the sixteen fork adapters (`FORK_ADAPTERS`), so a fork module's conformance load still
  keys on the adapters it requires.
- **D2, the SRC page table moves to `core/instrument-play`.** The fork's `MF_EXT` table (CANOPY's SRC pages and MOD routes) was a
  table of the machines image point. The adapter publishes it now: `[[info]] machines_ext_info` (`src/cf/src-ext.s`: magic `MFX1`,
  version 1, `mf_src_page_count`, `mf_src_apply_pages`, `instrument_mod_param`), which the machines service binds into
  `DKRT_MF_EXT` at its start, as before. `mf_src_page_count` answers 0 for a type it did not register, and the hook
  (`mf_srcpage_ext` in `hooks-dt2.s`) then asks the service's own native page count (`dks_native_page_count`), upstream's answer.
  The adapter's tick is an `[[info]]` tick of its own (`instrument_play_tick`), no longer an optional of the runtime.
- **D3, 72 parameter rows on the DT2.** `core/rows` reserves the parameter table in blocks and allows 64 rows; upstream's
  `packages/patchkit/tests/rows.rs` says no other module adds a row. The fork's `core/instrument-play` and `core/relay-track` add the
  named rows of their native MOD destinations (the MOD picker's), as they did in `core/params`, where the fork had raised the
  maximum to 72. The DT2 registry takes 72 again (the hardware set with RELAY uses 66), and the test lists the two adapters.
- **D4, resident table minor 6.** Upstream's own appends took minor 5; the fork's instrument entries (`InstrumentLinks`,
  `InstrumentViews`) stay at the end of the `links` and `views` groups as minor 6.
- **D5, DSP link window 4.** Its names stay `_dl_irows` and `_dl_iwords`: they are core/instrument-registry's DSP data, not an
  upstream `[[registry]] window`.
- **D6, the system package block: 360,448 bytes.** The package needs 357,424 bytes after the merge (the fork's instrument host and
  upstream's own growth); the block was 352,768 (upstream 327,680).

## Ported (step 3)

- `Provider::start_in` in the services `instrument`, `track-keyboard` and `relay` (their seams, `seam.rs`), the `svc-test` fixture
  of patchkit's tests, and the fork's tests of the machines and fx owners.
- `core/rows` for `core/dynreg`, `core/params` and the machine types: the requires of fxview, loop-control, phrase-control, the
  instrument-file fixture and instrument-play; RELAY's type check reads `rows_info` (`control.c`, `tape-sound.inc`, the seam's
  data); the machines registry reads `dkrt_svc_rows::rows_info`.
- The view capture of `core/ui`: `ip_view` reads `ui_machine_view`.
- The tick step table: the image ticks are the step `ImageTicks`, after the last of upstream's steps.
- The fork's host tests of the fx service bind the effect pages from device facts (`fxpage::bind_facts`) over the blocks of
  `rows_info`, in place of the fork's `fxpages_info` table; the machines tests start through `start_in` and set `rows_info`.
- The hook tests of `hooks-dt2.s` (`packages/patchkit/tests/machines_fw.rs`) link the `DKRT_MF_EXT` table (zero: the service alone).
- `runtime/core/owners.toml` maps `dkrt::stock_kit` (upstream's debug observer read, which upstream's map lacks).
- `tests/regression/host/canopy-native.c` drops the stub of the removed `mf_native_page_count`.
- `tests/patchkit/test_instrument_modulation_cf.py` is removed: its harness and fixtures are gone upstream (follow-ups).
- The fork's device tests take the helpers that upstream split out of `fx-live.ts` and `card-kit.ts` (RC5b U5) from their new
  modules (`guest-access.ts`, `dsp-state.ts`, `pcm-files.ts`, `panel-waits.ts`, `note-capture.ts`, the `card-*` files); `FxProfile`
  keeps the fork's optional `track_type_stride`.
- The fork's Python tests use the DSP link windows' new names (`_dl_win1_base`, `_dl_win1_rows`, `_dl_win2_base`,
  `_dl_win2_rows` for `_dl_vrows`, `_dl_vcount`, `_dl_frows`, `_dl_fcount`) and the lockfile at `ports/`. CANOPY's tests and its
  carrier probe reach the repository root from `modules/machine/digikit/canopy`, one level deeper since the earlier merge's layout;
  they failed on the fork's `main` too.
- The note of `docs/TESTING.md` on the fork's adapters names `FORK_ADAPTERS`.

## Regenerated (step 4)

`patchkit gen` (bindings, `dkrt.h`, `gen_*`), the seams of the three fork services (`--check` passes), the objects, and
`ports/dt2-1.17.toml`: upstream's resolved file plus the fork's 239 entries (192 anchors, 47 hooks), its hooks and their site facts
made again from the current `ports/fw` with `patchkit port facts` (the resolver cannot rebuild the DT2 function database here:
`cf.page.fn.source_turn` has no match, so the anchors are taken as resolved). Upstream's two removed list hooks stay out.

## Test metadata (step 5)

RC5b selects device tests by ownership: the fork's test projects drop their tags and name their subjects in
`implicitDependencies` (a service by its project and its crate); a module that a record loads only for the setup is `incidental`
with its reason (the machines service in CANOPY's and PRISM's records). The track keyboard's device test with CANOPY moved into
CANOPY's project (`modules/machine/digikit/canopy/tests/track-keyboard.device.test.ts`), whose subjects include the track keyboard: a
change to either selects it, and the track keyboard's tests no longer name a module (the layer check's exception for them is gone).

## Gates

On the host, on the WSL disk (2026-11-25); nothing ran on a device.

- **Rust.** `patchkit:rust-tests` (with `record-rules`) and `core/runtime:test-assembly` pass (the boot-effects snapshots
  rewritten: the service files and the system package are larger, `flush` sizes and file lengths only); `core/runtime:rust-tests`
  passes; the services' host tests pass (machines 158, fx 87, instrument, track-keyboard and relay 4 each), and so do the MIDI host
  tests (28).
- **Fit.** `patchkit:device-images` applies every DT2 set. The hardware set `canopy-overloop-prism-relay`: the container is 485.5 KB
  below the stage, cf.expansion has 120,100 bytes free (1,276,636 of 1,396,736 used).
- **Checks.** `patchkit gen --check`, the seams' `--check`, `patchkit:objects-check`, the layer check and the device-literal
  check pass (the baseline holds the fork's files beside upstream's).
- **Host suites.** `run-many -t test-host` passes (28 projects, the regression host tests among them).
- **Static.** `run-many -t lint,fmt-check,rust-fmt,typecheck,python-checks` passes (with the device-literal and test-source checks).
- **Python.** `tests/patchkit` and the services' tests: 1,213 passed, 59 skipped, 1 failed. The failure,
  `test_pk_current.py::test_ports_are_current`, needs the DT2 function database, which this machine cannot rebuild (the same limit
  as the resolver above). CANOPY's tests: 1,422 passed, 1 skipped.

### Step 7: the emulator, the rehearsal and the merge gate (2026-11-26)

On the DT2 1.17 and DN2 1.12 emulators; nothing ran on a device.

- **The fork's device suites.** CANOPY's (the track keyboard's CANOPY test, the step entry audition and the wheel and bend among
  them), the track keyboard's, Overloop's and RELAY's pass, and so do PRISM's but one. PRISM's coexistence test
  (`coexistence.device.test.ts`) loads its four modules now, then fails at its first note: it plays CANOPY's track with a TRIG
  press outside keyboard mode, and since the voice host (#19) such a press is silent on a CANOPY track. The panel hands a press to
  the track keyboard only in keyboard mode or in a step entry (`runtime/platform/keyboard-panel/src/cf/panel.c`), and the stock
  path has no voice for CANOPY. A probe on the 1.17 emulator, CANOPY on track 1: TRIG 1 with keyboard mode off peaks at -100 dB,
  TRIG 9 in keyboard mode at -33.2 dB. The fork's `main` has the same gap; there the test stopped earlier (see below). The fix is
  the panel's, not the merge's, and is left to its own change.
- **Upstream's device sets.** `core/runtime-machines-tests` and `core/services/fx-tests` pass; SHOT passes on the DT2. On the DN2,
  `shot-dn2-audio.device.test.ts` fails its retrigger phase: the sample is in its B part before the retrigger and in A after it,
  but both levels are 0.026 against a floor of 0.05. Upstream's own tree at the last integration (`c8f8a02c3`, `~/digikit-up`)
  fails it with the same levels, so it is upstream's, not the merge's.
- **Capacity and smoke.** `test-device--capacity--dt2` and `gate-device-smoke` pass.
- **Install rehearsal.** The hardware set `canopy-overloop-prism-relay` with the owner's settings record (the backup of
  2026-11-25): the five modules of the saved selection (CANOPY, Overloop, REMOTE, PRISM, RELAY) load with result 0 and no fault.
  The arena uses 352,384 of 450,560 bytes (98,176 free, the largest free block 85,344), the DSP area 227,648 of 524,224, the
  system package 357,424 of its 360,448, and the container leaves 497,072 bytes. Before the merge the same selection left 122,304
  bytes of arena free: `machines.dks` now takes 88,800 bytes of arena before its keys block (66,704 with it before), because RC4-4
  moved the machine list copy and the view refresh from the image into it; cf.expansion gained 21,212 bytes in return.
  `instrument.dks` and `track-keyboard.dks` are within 50 bytes of before.
- **Found and fixed by step 7.**
  - PRISM's and Overloop's device tests named `core/dynreg`, which upstream renamed `core/rows`.
  - PRISM's send test read the second track's type word through `track_type_stride`, which the fork's Python FX profile set
    (`0x450`) and upstream's TypeScript profile (`fxProfile`) does not: it is `TRACK_OBJ_STRIDE` beside `TRACK_OBJ_WORD` now.
  - PRISM's coexistence test loaded CANOPY without `core/track-keyboard`, which CANOPY requires since the voice host (#19): CANOPY's
    load answered `DK_E_MISSING` and the two effects waited behind it. This was broken on the fork's `main` too. Its image list and
    its `image-roots.json` set take the track keyboard.
  - `services/machines/dks/tests/image.rs`: the fork's `machines_ext_info` cases called the helper `table`, which upstream renamed
    `rows`.
  - The fixtures `local-patch` and `local-step`: their lock files gain upstream's `dkneeds` dependency of `dkrt-sdk`.
  - `dsp-stats.rig.test.ts` links the DT2 tail, whose fork sections import thirteen optional symbols (instrument, phrase, RELAY,
    pool and position providers): the rig gives them 0, as an image without their modules does.
  - `dsp-voice.rig.test.ts`: an unregistered type took 66 instructions to the stock path, where upstream bounds it at 60. The fork's
    dispatch code (`services/machines/dks/dsp/dispatch-dt2.s`) cleared the V2 scan flag on every voice and tried the V2 table for a
    type the mask had already refused, though the mask holds the V2 table's types too. A refused type now goes to the stock path at
    once, and the flag is cleared only where a scan starts: 58 instructions, upstream's bound holds. The V2 table's cost is voice
    0's, once per block (its address, and its types in the mask).
  - `tools/devtools/layercheck.py` as `ruff format` writes it.
- **The merge gate** (`DIGI_GATE_BASE=origin/main mise run test`): 246 tasks pass, 8 fail, none from the merge.
  - `digikit/overloop:test-dsp` and `digikit/prism:test-dsp`: the heaviest block is 6 cycles over the cost each module.toml declares
    (5,511 against 5,505; 22,192 against 22,186), as on the fork's `main`.
  - `gate-fx-dn2:fx-no-aot`, `gate-fx-dn2:tier-quick` and `tests/patchkit/test_pk_current.py` need the firmware function database,
    which this machine cannot rebuild.
  - `gate-fast-roots:fixtures`: the DN2 runtime fixture times out (wall clock), as in #17.
  - `tools:python-tests`, besides `test_pk_current.py`: `test_debug_module.py` and the two sample-pool tests fail the same way on
    upstream's tree (`git archive upstream/main`; the debug harness truncates a pointer to 32 bits, `debug.c` line 533). The
    pluck menu scenario (`test_menu_dt2.py`) fails in `pk_dkrt_boot.service_symbol`, which looks every read of `patchkit addr` up
    among the resident symbols of `runtime.maps`; `DKRT_DSP` is a system package static since upstream's `3615916b4` and is
    found through the host directory instead. Neither file differs from upstream.
  - `@digi/web:test-wasm`: the gallery requires a README.md of every shipped module and RELAY has none (as on `main`); two cases
    time out at 120 s under the gate's load.
  - `digiemu-desktop:rust-tests`: the system library `dbus-1` is not installed.

## Follow-ups

- **SRC page and modulation unit tests.** The fork's CF-unit cases of the SRC page hook and the MOD mapping
  (`11007c3cd:tests/patchkit/test_instrument_modulation_cf.py`, and lines 508 to 809 of
  `11007c3cd:image-modules/points/machines/tests/test_pk_machines_fw.py`) ran on the machines image point's fixtures and the Python
  CF-unit harness (`tools/patchkit/pk_cfunit.py`), all three gone upstream. They are removed here and wait for a port onto upstream's
  Rust harness (`packages/patchkit/tests/machines_fw.rs`, whose hook unit links `DKRT_MF_EXT` already). Until then the SRC page hook
  with the adapter's table (`mf_srcpage_ext`) and `mf_typearg`'s MOD answers have no unit test, only the device tests of CANOPY.
- **Service-run C under `runtime/platform`.** The C and assembler of the fork's three services still live in their adapters'
  folders (the seams' tables), where upstream's layout puts only flashed code.
- **A TRIG press outside keyboard mode on a CANOPY track.** Silent since #19 (above). Fixed after the merge on
  `fix/canopy-plain-trig`: the panel hands such a press to the track keyboard (`tk_panel_trig_on`), tested by
  `modules/machine/digikit/canopy/tests/trig-audition.device.test.ts` and PRISM's coexistence test. Before #19, on the tree of
  `741d059c5`, the press peaked at -33.2 dB, the level of a keyboard-mode press.
- **RELAY's README.** The web gallery requires one of every shipped module.
- **Upstream's.** `service_symbol`'s package reads (the pluck menu scenario), the debug harness's pointer cast and the sample-pool
  tests on a 64-bit host, and SHOT's DN2 retrigger levels: to report upstream, not to change here.
- **The install.** The bundle is rebuilt from the merged `main` and rehearsed again before anything is flashed.
