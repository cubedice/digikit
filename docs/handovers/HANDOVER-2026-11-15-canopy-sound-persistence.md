# CANOPY Sound persistence

Branch `codex/short-loop-proof`. The user asked for CANOPY's settings to persist
with the Sound and chose the real design over a boot cache or stopping. The
production adapter `core/instrument-play` now stores CANOPY's SRC settings
outside Sound memory and writes them into saved Sound records; see
[finding 127](../findings/127-canopy-sound-persistence.md). Emulator only. The
DTII still runs `bf49e4f08419374c` (the 2026-11-13 onset and chord-preview
install); nothing from this work is installed.

## What changed

- `sound-record.{c,h}`: the 40-byte record extension, format 1, in positions
  4..28 (step 4) and 77..89. `tests/patchkit/test_canopy_sound_record_sites.py`
  pins the writer zero-fill, the load loop and the parameter-0 positions.
- `sound-assoc.{c,h}`: the association store, 2,192 fixed slots and 497 sealed
  temporaries in 128 KiB from `fw_alloc` (allocated at the first Sound load, in
  task context with interrupts enabled). Host test
  `tests/regression/host/canopy-sound-assoc.{c,test.ts}`.
- `sound-persist.{c,s}`, `sound-load.s`: hooks on stock memcpy, memset and
  bzero (only lengths of at least 0x450 leave the stock path), the Sound
  writer, the initializer and the direct machine-type write; the loader
  wrapper keeps the record's extension across the stock body.
- `control.c`: `sa_follow` and `sa_publish`. An instance adopts its Sound's
  extension; a Sound without one keeps the instance's state and is claimed.
  Pattern switches therefore now switch CANOPY settings per pattern Sound.
- Declarations: `devices/fw/util.toml` (new: memcpy, memset, bzero),
  `machine.toml` (sound_serialize, sound_init_slices, sound_type_write),
  `project.toml` (kit table and Sound pool literals), and the missing
  declaration of `cf.page.hook.instrument_note_list_write` (its port record
  existed without one). This machine has no Ghidra cfdb or sharcdb:
  `out/port_shim.py` re-runs `port_resolve`, answers database rules from the
  existing port file (checked against the image bytes), refuses to write if any
  existing record would change, and `--insert` adds only the new records.
- Tests: `persistence.device.test.ts`, `copy-handoff.device.test.ts` and
  `pattern-copy.device.test.ts` (renamed from `pattern-copy-proof`, now always
  two CANOPY tracks) run the production adapter only and are registered in
  `modules/machines/canopy/tests/project.json`.

## Evidence

Emulator passes, all on the production adapter: project save and power cycle,
track copy/paste, pattern copy/paste/undo with one and two CANOPY tracks (the
finding lists the session folders). The first build hung boot: stock performs
about 30,000 copies or fills of at least 0x450 bytes before ready, and the
unfiltered store scanned every temporary with interrupts masked for each one.
The page filter fixed it; all of those now return after a few compares.

Final check of the committed form: `pnpm nx:pilot run canopy-arp-tests:test-device`
passed all four registered tests (persistence, copy-handoff, pattern-copy, arp);
the opt-in research proofs skip. Rebuilding `core/instrument-play` after the
last `control.c` edit (the host-only `__m68k__` guard) changed only the
object's `source_sha256`; the patched `canopy-overloop` image is unchanged
(`9b7bf38c…`). The new `cf_window_hash` sites resolve uniquely to the recorded
port addresses through `port_resolve.resolve_cf_window_hash`, and the bytes
there decode to the instructions the declarations name.

Preset pool and +Drive (`sound-transfer.device.test.ts`, registered): pool save
and load onto another track and back, +Drive save and load in the same session
and onto a new CANOPY track after a power cycle all carry the settings; see the
finding for the values and the menu paths. Shared panel steps are in
`modules/machines/canopy/tests/canopy-panel.ts`.

## Gate cleanup (second part of this session)

- `host/canopy-preset.test.ts`: its 7 failures came from `38956214` (the
  four-page SRC layout moved WAVE, COLOR, DRIFT and the wavetable/FM controls);
  the scenarios now turn the moved encoders, and the harness `srcstate N`
  reports encoder N's display. 105/105 pass.
- Formatting: prettier (33 files), ruff (lint and format), `cargo fmt` on the
  gate's crate list (4 `packages/patchkit` files). `cargo fmt --all` still
  differs in `dkrt-sdk`, `dkrt-fit`, `dkneeds` and `dkfw`, which the gate
  excludes because their sources are hashed into committed objects.
- Layercheck holds. CANOPY's Python tests, its three generators/probes
  (`modules/machines/canopy/tools/{fm_tables,wavetables,carrier_probe}.py`), its
  movement host test and its runtime proofs (now `*.manual.device.test.ts`, run
  by name, outside the Nx gate like before) moved into the module; Overloop's
  test, conformance provider and display proof into `modules/fx/overloop/tests`.
  The midi-events composed-frame test is shared (`tests/support/midi_frame.py`)
  and CANOPY runs its own consumer case; the FX conformance worker loads a
  module's `tests/fx_provider.py`. Two exceptions are named in `layercheck.py`:
  the checker's own include lists, and `core/instrument-play`'s settings hook,
  which compares CANOPY's id until the runtime publishes the instrument's
  module record.
- Committed objects were stale before this session: `dsp-ddr-proof` and
  `dsp-lp0-proof` (SDK headers changed after their build) and the
  `loop-controls`, `loop-display`, `loop-storage` DSP objects (assembled with
  selache `2b26d3b7`, not the pinned `02d6d785`). All rebuilt; only those
  fields changed. On this machine `test_pk_current.py` needs
  `DIGIKIT_CODEGEN_DIR=$HOME/.cache/digikit/canopy-codegen-gcc`.

## Open

- Hardware: not installed. An install needs the user's approval and the
  established preset-4 preservation. Check on hardware: a saved project after a
  real power-off, return to stock firmware with a project saved here, and the
  cost of the global memcpy hook on a busy device.
- `test_canopy_native_amp_firmware.py` failed 8 of 20 since `24637bda`, whose
  onset de-click (now a 256-sample minimum, `06369065`) made CANOPY's attack
  deliberately slower than stock for attacks under 256 samples; the test,
  which skips without the local DSP extraction, was not updated. It now
  compares with stock at raw attack 13 (272 samples, the fastest the minimum
  leaves alone), each note-off in the phase its case names; 20/20 pass.
  `test_canopy_v2` pins the 256-sample ramp itself.
- SysEx is blocked in the emulator: a pattern dump stops after 4032 bytes
  because USB transmit completion is not modelled (finding 127).
  `sysex-pattern.manual.device.test.ts` is ready for when it is; modelling it
  means finding what the firmware waits on after the USB transmit function
  that `midi.usb.out` observes, and raising it in `packages/emulator`.
- `test_canopy_v2.py` (911 cases on the Python SHARC interpreter) took 5,035 s
  in one process, over the 3,600 s per-file limit of `python-tests`. Its call
  helper now keeps DM in a forked `pk_sharc.CallSession` with fresh registers
  per call instead of copying about 50,000 words in and out of every call (all
  911 cases checked call by call against the old helper: identical); total CPU
  fell by a quarter, the heavy arp/FM cases are bound by instruction count.
  The file declares `# python-tests shards: 8` (`test_canopy_voice.py`: 2), and
  `python-tests` runs the shards in parallel (`tools/devtools/pytest_shard.py`):
  CANOPY's module tests took 21 minutes of wall time with 6 jobs, the slowest
  shard 608 s. CANOPY's DSP tests (v2, voice, arp_attack, fm_quality,
  instrument, instrument_play) now run on the native SHARC core when its
  library is built (`mise run gen:native-lib`; `pk_sharc.session_backend`),
  each file passing `PK_SHARC_BACKEND=check` (every call on both cores, equal)
  and native alone: v2 takes 86 s in one process, all CANOPY module tests 147 s
  of wall time. Without the library they run on the Python core as before.
  `tools:python-tests` builds the library first (`tools:sharc-native-lib`,
  `sharc_native_lib --ensure`: about 1 s when current, 35 s after a
  `sharc_core` change), so the gate runs them natively.
- Not exercised: kit reload, machine change and back (expected: the
  instance's state is kept, as before), and LEARN during a save.
- Copies made by code other than memcpy/memset/bzero are not followed. The
  write-watch inventory of the pattern-copy test found none (finding 127);
  operations outside that test are not inventoried yet
  (`out/run-persist-ww.sh TEST NAME`, `out/sidetable_ww_report.py LOG`).
- The superseded `+0x13a` fixtures (`tests/regression/fixtures/canopy-*`,
  their opt-in device tests and the `DK_CANOPY_SOUND_PROOF` code in
  `control.c`/`sound.c`) patch the same instructions as the new hooks and
  cannot be combined with this adapter. They are kept as research for now.
- `out/port_shim.py`, `out/run-persist-ww.sh` and `out/sidetable_ww_report.py`
  are local and ignored, not committed. A machine with the Ghidra databases
  should re-run `port_resolve` normally and confirm it reproduces the new
  `dt2-1.17` records.
- `mise run test` at `306eee25` (WSL, repository on `/mnt/c`, no DN2
  firmware): exit 1 after 80 min, 113 tasks passed, 40 failed. Passed:
  lint, formatting, types, host tests, every CANOPY Python test (on the native
  core), `pk_sharc`, `test_outcomes`, layercheck, `canopy-arp-tests`, the DT2
  smoke cells (9/9), card check, stock and DT2 FX suites, and DT2 conformance
  of every module but two. Failures by cause:
  - No `firmware/Digitone_II_OS1.12.syx` here, so no DN2 daemon or generated
    core: the DN2 smoke cells, `plusdrive_check --dn2`, three Rust test tasks,
    and every device or conformance file with DN2 cases. Those files throw at
    load, so their DT2 cases did not run either.
  - `nx-tools:test-host`: `spawnSync pnpm ETIMEDOUT` (Nx takes about 40 s to
    start on `/mnt/c`, the tests allow 30 s).
  - DT2 conformance, older than this session: `digikit/canopy` requires
    `core/instrument-play`, which the `emulator` patch set the harness always
    boots lacks; `digikit/overloop` references `_loop_buffer_info` and
    `_pattern_position_info` (`core/loop-storage`, `core/pattern-position`) but
    declares `requires = []`.
  - Python, same counts as the 2026-11-14 run before this session:
    `test_instrument_registry` and `test_instrument_mailbox_lifecycle` (their
    link lacks `_instrument_play_status`, which `midi-events` `reply.s` uses
    since `b355a2d4`), `test_dsp_sample_binding_close`, `test_pk_machines`
    (`cf.mem.fn.new` for dt2), `test_sample_pool_core`, `test_sample_pool_dn2`,
    `test_toolchain_install` (patches `sys.platform`; `pk_toolchains` reads
    `DARWIN` at import). `test_snapread` reads a local `out/snapshots` file.
  - `test_equiv_dt2`: the scenario card cache under `out/` on `/mnt/c` holds
    each sparse card in full, so every new card evicts the others in use. With
    `DIGIKIT_CACHE=$HOME/.cache/digikit/scen-cache` it passes 4/4.
    `test_menu_dt2` fails 2/2 alone too (`menu-dkrun-pluck`: the SELECT record
    lists `debug.dkm` besides `pluck.dkm`; `menu-transfer-pluck`: no new boot
    within 90 s); this session's shared changes are additive port records,
    declarations and `cargo fmt`, which do not reach it. Cause not found.
  In WSL also set `TMPDIR` under `$HOME` (`host/runtime.test.ts` refuses a
  7.8 GB tmpfs `/tmp`) and `DIGIKIT_CODEGEN_DIR`.
