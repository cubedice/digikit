# CANOPY and Overloop on upstream main 70d8313f

The branch `codex/short-loop-proof` (f619d899, also `backup/short-loop-proof-2026-11-15`) was squashed into eleven topic
commits and rebased onto main `70d8313f` (`rebased/short-loop-proof-raw`, bb0a45fb), then ported to upstream's restructured
runtime and SDK on `squash/short-loop-proof`. `codex/short-loop-proof` itself is unchanged and nothing was pushed: the
ported branch replaces it only after review, and that needs a force push.

## What the port changed

- V2 instruments on upstream's runtime: the loader detects a version 7 or 8 DSP section and queues it for the V2 owner
  (`instrument/pending.rs`), which publishes the CF side through upstream's `complete` once the DSP acknowledges the
  instance (`loader::publish_v2`). The legacy policy gate moved into `link.rs` (`DspTransport::send` refuses unless Run).
  `dkrt_settings_get` is back for `core/instrument-play`.
- Module contract: CANOPY and Overloop declare their machine and effect with builder calls (`src/module.c`);
  `[[machine]] instrument = true` makes patchkit pack the version 8 section (`major3.rs`, `dkmdsp.rs`). The branch's C
  adapters use the `dk_ui_menu_*` names, and the Overloop control tick and MIDI dispatch moved into `core/platform`.
- Per-device staging: the device object owns `DKRT_STAGE` and `dkrt_stage_bytes` (`src/cf/<dev>/device.s`), from
  `[limits] stage_bytes` of the device file: DT2 160 KiB, DN2 64 KiB. `load_to_stage` reads a file in bounded native reads
  (`read_extent`), as the branch did: one read of more than 64 KiB fails with `E_BAD_STORE`.
- CANOPY's factory wavetables are 16-bit samples (finding 350 has the sizes); the table output is scaled by 2^-14 once,
  exact for every stored sample. That let the DT2 stage drop from 240 to 160 KiB.
- `[uses.dt2]`: CANOPY commits one machine row (without it the commit refuses with `E_DECL_USES`), Overloop one effect
  record. CANOPY's SRC pages and parameters are not runtime records: `core/instrument-play` draws them over the stock SRC
  page. Overloop's counts are measured by `module:check`; CANOPY's are counted from its builder calls.
- DSP encodings under the pinned Selache `967c1d1e`: the branch sources keep their old encodings (conditional bare moves
  as transfers, unconditional ones as PASS, the envelope's DAG load pre-modify).
- Findings 76-81 and 115-127 of the branch are now 133-151, above upstream's numbers.
- Tests: CANOPY's registered device tests take their image from the `instrument-play` set of
  `tests/regression/runtime/image-roots.json` and run on the DT2 only (`describeOn`); the manual proofs with test roots
  patch live (`runtime-plan-modules.ts`). The rig conformance gives no voice spec for a V2 instrument and links
  `core/dsp-voice`'s optional `_instrument_voice_rows` as zero. A module states its conformance needs in its own
  module.toml (`[conformance] patch_set` for the load cell and `module:check`'s commit stage, `rig_skip` for the rig;
  docs/TESTING.md), so no shared file names CANOPY or Overloop.
- The load cell (`tests/conformance/assert-load.ts`) expects no runtime voice row of a V2 instrument (`rows` 0):
  `core/instrument-play` publishes its row in its own table when a track admits it. The other checks are unchanged.
- `module:check`'s commit stage (`tools/devtools/commit_records.ts`) boots the module's `[conformance] patch_set`; it always
  booted `emulator`, so a module that requires a core module outside it was refused at load and could not be measured.
- The kit's `VoiceRig` links the branch's two `core/dsp-voice` data words (`_instrument_voice_rows` as zero or a test
  table, `_dv_v2_scan`); without them every dsp-voice rig test failed to link. The branch's two V2 carrier-table cases
  (legacy priority, a V2 row on any track) moved from the retired Python rig to `dsp-voice.rig.test.ts`.
- Host tests: the CANOPY C harness (`tests/regression/host/canopy-settings.c` and the files that include it) stubs the
  `dk/platform/ui.h` menu API, and `settings-retry.rs` stubs the crate items upstream's `settings.rs` uses. Upstream deleted
  `settleUi` and renamed `expectRuntimeUi`'s `timeout` to `emulatedMs`; the branch tests wait the same 12 s with
  `waitEmulated`.
- Fixed in `core/instrument-play`: `src-native.c` declared its own one-argument `dk_ui_toast`, so on the device its lock and
  fault toasts passed the text as the UI handle. It now toasts through the core UI handle, as `control.c` does.
- The branch's C (133 files) is formatted with the pinned clang-format 22.1.4. Whitespace only: the byte hash of every
  committed object (`tools/nx/object-hash.mjs`) is unchanged; the nine rebuilt objects changed only `source_sha256`.

## Fixed after the merge gate

- b41db8e4: CANOPY's `src-waves.h` and `src-envelope.h` are its generators' output again and listed in
  `.clang-format-ignore` (`test_ui_tables.py` compares them with the generator); `play-cf`'s bytes did not change. The
  Python tests that expected a 256 KiB stage expect DT2 160 KiB and DN2 64 KiB (`test_pk_device`, `test_patchkit_dkrt`,
  `test_patchkit_dkrt_machine`, `test_digikit_menu`). `test_pk_machines` accepts an extern that names another
  subsystem's rule (the native SRC page list grows through `cf.mem.fn.new` and `cf.mem.fn.free`). The mailbox lifecycle
  test's reserved-word case uses status flags 2: 0, 1, 3 and 7 are legal.
- fd4d7bb4: upstream's Transfer source panel offers a source over 256 KiB as a download (`TransferSource.download`, the
  web fetchers' URL); the bundler already kept CANOPY's 708 KB `wavetables.s` out of the text viewer.
- Upstream's `samples-store-live` device test: 547daf4e made the store listing report the payload size and keep the
  stored size (payload and trailer) in `stored`, and left the test comparing `size` with the stored length. The branch
  changes neither file, so the DN2 half fails on main too (not run there). It now checks both fields.
- 14e7a29b, upstream's wasm test: da426d17 pinned Selache 967c1d1e, which words the refusal of `DM(15, I4) = I0`
  differently (`I0 cannot be stored through its own DAG`); the test matched the old text. It now checks patchkit's own
  report (`selas failed`, the line).

## Gaps closed on 2026-11-16

- V2 needs. `needs_of_v2` refused CANOPY's real file: it wanted descriptor flags 0, and the loader's decoder takes 0, 1,
  3 or 7 (CANOPY has 7). It also counted the legacy arena (the module block and a transfer block), while `loader::load`
  keeps one block for a V2 file: the module, copies of its keys table and DSP section, and the output buffer of the
  relocated image. Both follow the loader now. The hosts read with it (`patchkit needs` and `dump`, the wasm `pk_needs`),
  so `dk run` no longer refuses CANOPY as a file the loader does not accept and `module:check` no longer prints "the
  loader refuses the file". The runtime's scan reads with it too, so the MODULES page gives CANOPY a kind, its memory and
  a fit verdict, and counts it when another module is ticked beside it. The scan entry and the fit rule still have no
  instrument rows: the loader's V2 owner admits those. `modules/machines/canopy/tests/needs.device.test.ts` (DT2)
  compares the needs with CAPACITY after a load (arena 279 600, DSP area 150 016, one machine row, no voice row) and
  reads the MODULES row. The runtime object grew by about 4 KiB; the `emulator` set of both devices and the
  `canopy-overloop` set of the DT2 still apply.
- Overloop's DSP cost. The rig's effect suite skips Overloop, so nothing measured `dsp_cost_fx` and the boot planner
  budgeted it as free. `[uses.dt2] dsp_cost_fx = 2606` is the costliest hook call of the scenarios in
  `tests/test_overloop.py`, which now holds every block to it. A rig fixture was not built: with a stand-in position
  record the rig would run only the idle path.
- `module:check` on a V2 instrument: the commit stage boots the real DSP for a module with `[[machine]] instrument = true`
  (`commit_records.ts --dsp sharc`), because such a module commits only after its DSP acknowledged the instance.
  `module:check canopy --device dt2` ran to the end for the first time: one machine row and 124 record bytes committed
  (`[uses.dt2]` unchanged), 1394 Python tests, the six DT2 device files, "fits an empty device". It wrote CANOPY's
  `build/gen/gallery.json`.
- `module:check` runs a module's device files through Nx without `--parallel=1` (the launcher's budget and the emulator
  slots bound it): CANOPY's six files took 19.5 minutes one by one.
- The web bundle's CRLF normalisation was already ported with fd4d7bb4 (`bundleText` in `gen-modules.mjs`, the three
  call sites of the branch).

## Behaviour change

- Overloop's knobs: an effect param of a builder call goes on the next knob and sets the word of its position, and the SDK
  has no call that puts it elsewhere. The knobs are now REC, START, FADE, LENGTH (the old declaration put LENGTH on the
  third knob and FADE on the fourth). The DSP words did not change: FADE is word 2, LENGTH word 3.

## Evidence (DT2 1.17 emulator, WSL)

- `canopy-arp-tests:test-device`: arp, copy-handoff, pattern-copy, persistence and sound-transfer pass; the DN2 halves
  skip.
- `digikit/canopy:test-conformance` and `digikit/overloop:test-conformance` pass on the DT2 (the DN2 cells skip). Neither
  passed on the branch.
- `module:check overloop --device dt2`: the measured rows match module.toml (0/0/0, 436 record bytes), its 25 own tests
  pass, and it wrote `build/gen/gallery.json` (REC, START, FADE, LENGTH), which the knob test reads.
- `run-many -t test-rig`: all 10 projects pass. `@digi/sharc-rig-kit:test-conformance-rig` 210/210 (every machine and
  effect, both devices); `core/dsp-voice:test-rig` 155 passed (the DN2 stereo files skip without DN2 data).
- `regression:test-host` 55/55 files (565 tests), `core/runtime:rust-tests` 187/187; lint, fmt-check (prettier and C),
  rust-fmt, typecheck and python-checks pass on every project.
- CANOPY's Python tests on the native SHARC core, including the 911 `test_canopy_v2` cases with the 16-bit kernel.
- patchkit, dkneeds, dksharc and the runtime host tests; `prims.rs` did not finish within 900 s on this machine (a
  timeout, not a failure).

## Merge gate (fd4d7bb4, 2026-11-16, WSL, DT2 1.17 and DN2 1.12)

`mise run test` (affected from main) ran for 101 minutes: 349 tasks passed, 19 failed and 8 did not run behind them.
Passed: `digikit/canopy` and `digikit/overloop` `test-conformance`, `@digi/sharc-rig-kit:test-conformance-rig`,
`core/dsp-voice:test-rig`, and 84 DN2 device halves (among them `core/fx`, `core/fxpages`, `core/dynfx` fx-chain,
`core/digikit-menu`, `digikit/shot`, `digikit/pluck`, `tools/bootscreen`). No failure is in code the port changed:

- `patchkit apply` killed at 30 s (no stderr), 9 tasks: the load cells of `digikit/probe`, `digikit/freqshift`,
  `tom/tb303`, `digikit/shot`, `fadedlimes/bd` and `fadedlimes/analog`, `core/fx` fx-send DT2, and `core/dynfx` fx-reuse
  on both devices. The checkout is on C:, which WSL reads through drvfs: `apply --set emulator` takes 9.2 s there
  (1.2 s of CPU) and 1.4 s from a copy of the tree on WSL's own disk, with identical bytes. With seven emulators
  running it passes 30 s.
- Load timeouts: `@digi/web:test-wasm` (every case over 120 s, 98 minutes in all), `tools/nx/tests/selection.test.ts`
  (`spawnSync pnpm ETIMEDOUT`), `gate-fast-roots:fixtures` (a wall-clock timeout building the DN2 reset fixture) and
  `tests/devtools/test_test_outcomes.py` (18/18 alone). The daemons run without PGO here (about 30% slower DSP; see PGO
  below).
- Upstream or this machine: the `sharc-native` test `fast_mode1_write_that_enables_interrupts_matches_the_interpreter`
  (the branch does not touch `packages/emulator/sharc`); `patchkit.wasm is small` (over 2.5 MiB: main's is 2,666,749
  bytes, this branch's 2,683,417); `gate-fx-dn2` `fx-no-aot` and `tier-quick` (`E2BIG`: `SHARC_FAST_REGIONS` is 568 KB,
  over Linux's 128 KiB limit for one environment string); `digiemu-desktop:rust-tests` (no `libdbus-1-dev` in WSL);
  `test_snapread` (the local snapshots are memory-only) and `test_pk_current` (no `out/cfdb/dt2-1.17-emac.sqlite`).
- Upstream's `samples-store-live` DN2 half: fixed above; it passes alone.
- `core/runtime-storage-tests` DT2 (`no reply to MidiRpc 11` after 41 s): it passes alone, in 16 s.

The same gate from a worktree on WSL's ext4 disk (`~/digikit-ext4`, branch `lane/ext4`, d6b2e207; it shares the clone's Nx
cache, `out/` and the virtualenv) took 39 minutes: 359 passed, 9 failed. The `patchkit apply` timeouts and the storage
and sample-store failures were gone. Of the nine:

- `core/dynfx` fx-reuse DN2 (`no new boot within 90 s`) and `nx-tools:test-host` pass alone on the idle machine: load.
- `@digi/web:test-wasm`: 72 of 74 cases pass alone. The two left were the assembler wording (fixed above) and the size
  check (upstream).
- `gate-fast-roots:fixtures` fails alone too: after the DN2 soft restart a 5 s emulated step does not end within its
  300 s wall limit (about 2.9 s emulated, 4.0e9 instructions). It fails the same way, at the same point, with the image
  patched from main's modules (`DIGI_REPO_ROOT` on an archive of 70d8313f, the same daemon), and with its output on
  ext4: not the port, not NTFS. Open upstream or on this machine.
- `tools:python-tests`: `test_test_outcomes.py::test_end_to_end` gives a pytest process 2 s per file and fails under
  load (18/18 alone); `test_snapread` and `test_pk_current` as above.
- `sharc-native`, the two `E2BIG` targets and `digiemu-desktop` as above.

At 613ce553 (the gap fixes; the runtime object changed, so every device test ran again) the static checks pass and the
gate from the worktree took 91 minutes with 14 failures, then 30 minutes with 8 on a second run that replayed the passed
tasks and used three emulator slots (`DIGI_EMU_SLOTS=3`). The first run ran out of memory: WSL had its default 15 GB,
and seven emulators beside the builds gave `Cannot allocate memory` (`DSP image`, `CF snapshot`, the DT2 ready step),
which failed `tom/tb303:test-conformance`, `gate-smoke`, `gate-dt2-tone`, `gate-fx-dt2`, `core/runtime-storage-tests`
DT2 and the 120 s cases of `@digi/web:test-wasm`. All of these pass in the second run. `.wslconfig` now gives WSL 24 GB
and 16 GB of swap. The eight left:

- `gate-card:card` is the worktree's: the run directory `~/digikit-ext4/out/runs/card-check/<stamp>/run` is short enough
  for the daemon to put its control socket there (`socket_path`, 100 bytes), and `out` is a link to C: (drvfs), which
  has no Unix sockets (`Operation not supported`). From the C: checkout the path is too long and the socket goes to
  `TMPDIR`. `card_check --run-dir` on ext4 passes (three tracks, no underruns).
- `tools:python-tests`: `test_snapread` and `test_pk_current` as above. `test_pk_machines_dn2_fw.py` failed one case in
  the first run and `test_patchkit_cmodules.py` one in the second, each passing in the other; both files pass alone
  (18 passed).
- `@digi/web:test-wasm`: only the size check (73 of 74 cases pass; the assembler wording is fixed).
- `sharc-native`, the two `E2BIG` targets, `gate-fast-roots:fixtures` and `digiemu-desktop` as above.

PGO: the gate's "profile is missing" warning is wrong on this host. `native_pgo.supported_host()` is aarch64 macOS only:
`train` refuses here and the builds read no profile, so the daemons always run without PGO on Linux.

## Open

- Not run: `mise run test:full` on this branch.
- Disk: a session refuses to start under 50 GB free. A card image is the whole 24.8 GB card, sparse: on NTFS through
  WSL (C: or D:) it is fully allocated and its build passes plusdrive's 30 to 60 s timeouts, so `dist/session-cache` is
  a symlink to `~/.cache/digikit/session-cache` on WSL's ext4 disk (about 140 MB with the cards). `out/cache`, the old
  card images and a 580 GB session cache on D: were deleted on 2026-11-16 (all rebuild on demand).
- `expectRuntimeReady` and the FX fixtures run `patchkit apply` with a 30 s timeout (`runtime/patchkit-exec.ts`): on this
  machine that holds only on an idle machine or with the checkout on WSL's disk (see the gate above).
- Overloop is not run by the rig's effect suite (`rig_skip`): its DSP reads `core/loop-storage`'s and
  `core/pattern-position`'s data, which the rig does not place. Its own tests run it with stand-ins and bound its cost.
- The scan and fit protocol has no instrument rows or tracks: two V2 files ticked together get a verdict on memory only,
  and the loader refuses the second. A capacity kind for them would close it (an ABI change).
- Gate failures that cannot pass on this machine and are not the port's: the `patchkit.wasm` size limit, the two `E2BIG`
  targets, the `sharc-native` `fast_mode1` test, `digiemu-desktop` (libdbus), `test_snapread`, `test_pk_current`, and the
  DN2 reset fixture of `gate-fast-roots`.
- A worktree whose `out` is a link to drvfs cannot hold a daemon's control socket in a short run directory
  (`gate-card`): `socket_path` could fall back to `TMPDIR` when the bind is refused.
- Hardware: not installed.
