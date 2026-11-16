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
- CANOPY's factory wavetables are 16-bit samples (finding 150 has the sizes); the table output is scaled by 2^-14 once,
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
  `tests/devtools/test_test_outcomes.py` (18/18 alone). The PGO profiles are missing here, so the daemons run without
  PGO (about 30% slower DSP).
- Upstream or this machine: the `sharc-native` test `fast_mode1_write_that_enables_interrupts_matches_the_interpreter`
  (the branch does not touch `packages/emulator/sharc`); `patchkit.wasm is small` (over 2.5 MiB: main's is 2,666,749
  bytes, this branch's 2,683,417); `gate-fx-dn2` `fx-no-aot` and `tier-quick` (`E2BIG`: `SHARC_FAST_REGIONS` is 568 KB,
  over Linux's 128 KiB limit for one environment string); `digiemu-desktop:rust-tests` (no `libdbus-1-dev` in WSL);
  `test_snapread` (the local snapshots are memory-only) and `test_pk_current` (no `out/cfdb/dt2-1.17-emac.sqlite`).
- Upstream's `samples-store-live` DN2 half: fixed above; it passes alone.
- `core/runtime-storage-tests` DT2 (`no reply to MidiRpc 11` after 41 s): it passes alone, in 16 s.

## Open

- Not run: `mise run test:full` on this branch, a gate pass of the failures above on an idle machine or a checkout on
  WSL's disk, and `module:check canopy`. The last is not tried: its commit stage waits for `MODULE_COMMIT`, which a V2
  instrument should log only once its DSP acknowledges the instance, and the stage boots with a zero DSP.
- Disk: a session refuses to start under 50 GB free. A card image is the whole 24.8 GB card, sparse: on NTFS through
  WSL (C: or D:) it is fully allocated and its build passes plusdrive's 30 to 60 s timeouts, so `dist/session-cache` is
  a symlink to `~/.cache/digikit/session-cache` on WSL's ext4 disk (about 140 MB with the cards). `out/cache`, the old
  card images and a 580 GB session cache on D: were deleted on 2026-11-16 (all rebuild on demand).
- `expectRuntimeReady` and the FX fixtures run `patchkit apply` with a 30 s timeout (`runtime/patchkit-exec.ts`): on this
  machine that holds only on an idle machine or with the checkout on WSL's disk (see the gate above).
- Overloop is not run by the rig's effect suite (`rig_skip`): its DSP reads `core/loop-storage`'s and
  `core/pattern-position`'s data, which the rig does not place. Its own tests run it with stand-ins. A rig fixture for
  those two would remove the exemption.
- `patchkit needs` reports CANOPY's DSP section as malformed: the legacy reader refuses version 7 and 8 sections, as on
  the branch before the port, so the device menu shows no needs for CANOPY. `needs_of_v2` reads them.
- Not ported from the branch: the web bundle's CRLF normalisation (upstream reworked those files). Large-source
  downloads are ported (fd4d7bb4).
- Hardware: not installed.
