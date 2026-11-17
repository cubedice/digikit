# Upstream 839a43e1 merged into the fork, and the integration work after it

DT2 OS 1.17 and DN2 OS 1.12, WSL emulator, 2026-11-16. The port of CANOPY and Overloop is in
[HANDOVER-2026-11-15-port-to-main.md](HANDOVER-2026-11-15-port-to-main.md); this file starts where the fork's `main` took
upstream's 135 commits (70d8313f to 839a43e1: R11, the R12 service steps, the loader split, the wait API of the tests).

Branches: `main` holds the merge (5d3aae91) and the test compatibility commit (4cdec032), both local. `integrate/upstream-r12`
holds everything below on top of them. Nothing is pushed.

## The merge (5d3aae91, 4cdec032)

- Ten conflicts. The one with content: upstream split `loader.rs` into `loader/`, generic over `Images` and `Registry`. The V2
  instrument load path (admission, the kept keys table and DSP section, deferred publication through `publish_v2`) was re-applied
  to `loader/mod.rs`; `boot::note_result` is `report_dsp` now.
- Upstream deleted `waitEmulated`, `waitEmulatedFrames`, `tap`, the tap form of `openDigikitMenu` and `panel.ts`, and forbids
  wall-time sleeps in module tests (oxlint). Thirty test files of the port take the same steps from
  `tests/regression/runtime/port-compat.ts`; their fixed sleeps became emulated time.
- Upstream had fixed two of the port's test fixes itself (`samples-store-live`, the assembler refusal text): upstream's versions
  are taken.

## What the first gate on the merge found (84f28442)

- **The CANOPY patch set did not fit.** [D] `patchkit apply --set canopy-overloop` on the DT2: `core/instrument-play: 88072 bytes
  do not fit in cf.expansion (1316800 of 1396736 bytes used)`, 8136 bytes over. The merged runtime object is 1 019 355 bytes
  against 987 358 before the merge (upstream's own grew by 32 129). The DT2 staging buffer went from 160 KiB to 148 KiB
  (`[limits] stage_bytes = 151552`): CANOPY's file still fits, and the set fits again.
- CANOPY's device tests pin the DT2: their `runtimeAcceptance` records say `"devices": ["dt2"]`, as upstream's device-halves
  check of `tools/nx` asks.
- `apps/web/tests/dktransfer.test.ts` (upstream's file): `Object.fromEntries` over rows keyed by `string | null` is untyped,
  and `astro check` fails on it. Keyed by `id ?? path`. Upstream main has the same failure.

## Integration work (`integrate/upstream-r12`)

| Commit | What |
| --- | --- |
| 7589cd55 | The fork's findings 136 to 151 are 336 to 351 (upstream is at 135 and numbers in sequence); every link and "finding N" follows; `docs/FINDINGS.md` holds them as rows. |
| 7fce7308 | The rules of a V2 instrument section are defined once, in `dkneeds::instrument` (header tables and their order, limits, the twelve descriptor words, a text range, a callback); the loader's validator (`dkrt_sdk::instrument_section`, `instrument_descriptor`) takes them from there. `dkrt-sdk` depends on `dkneeds`. One canonical fixture, `dkneeds::fixtures::v2_section` (feature `fixtures`), for the tests of `dkneeds`, the SDK and patchkit. |
| 53002960 | The loader's V2 path is three methods of its `Images` seam (`instruments_idle`, `admit_instruments`, `queue_instruments`); `loader/tests/v2.rs` loads the canonical section over the fakes. |
| 23d735e4 | `INSTRUMENT_ROWS` and `INSTRUMENT_TRACKS` are CAPACITY kinds 17 and 18 and resources 7 and 8 of `USES`; the packer writes them, `dkrt_fit`, the boot plan, the menu's scan and `dkrt_capacity` count them. |
| 025d1417 | CANOPY's six registered device tests use the wait API (`ui.wait({ ms, why })`, `flows/menu.ts`) through `tests/regression/runtime/session-rt.ts`. |

Facts from this work:

- [D] The two readers of a V2 section had drifted apart twice: the descriptor's flag word (fixed in the port), and the needs
  reader takes a version 7 section and V1 voices or effects beside instruments, which the loader refuses. The second is kept
  and written down (`dkneeds::instrument`, module text): the packer can still write both.
  `the_needs_reader_and_the_loader_agree_on_every_one_word_edit` (SDK crate) holds the two readers to one answer on every
  one-word edit of the canonical section.
- [D] `loader/tests/v2.rs` proves on the host that the block the loader allocates for a V2 file is the arena the needs reader
  reports for it (`a_v2_file_is_placed_in_one_block_and_handed_to_the_owner_unpublished`).
- A boot takes one file of persistent instruments (`instrument/pending.rs`, `occupied`). The fit rule says it with `largest`
  of `INSTRUMENT_ROWS` (what a file can still take; `take` sets it to 0), so a second file in a selection is `E_PLAN_FIT`
  before anything loads. A device without the firmware adapter reports 0 rows.
- [D] After the capacity change the CANOPY set leaves 3056 bytes of `cf.expansion` on the DT2, and the staging buffer has
  4184 bytes over CANOPY's file. Both margins are small: the next growth of the runtime or of CANOPY needs room from somewhere.

## Not done, and why

- **The instrument code as a service crate.** About 10 000 lines of `instrument_*` live in `dkrt-sdk` and the owner in the
  runtime core. Upstream's service form needs the lifecycle hooks of its runtime slimming plan (`module_begin`,
  `module_commit`; `docs/plans/runtime-slimming.md`, step C1, not started). A move before that changes forty files and gives
  the layout without the contract. Do it when C1 lands.
- **The adapters as `.dks` services.** `core/instrument-play`, `loop-storage`, `pattern-position` and `loop-display` all have
  DSP code, and a service with a DSP section is refused until upstream's R12-8.
- **The manual proofs.** Nineteen `*.manual.device.test.ts` files and the CANOPY proofs under `tests/regression/runtime` still
  use `port-compat.ts`; no gate runs them, so a rewrite of their waits could not be proven here. Nine of the proofs still sleep
  in wall time (outside the lint's scope).
- **Upstream pull requests.** Three branches off upstream main hold general fixes, one commit each, local and not run on
  upstream's tree: `upfix/web-dktransfer-rows`, `upfix/modcheck-parallel`, `upfix/bundle-lf`.

## Open

- [O] `cf.expansion` on the DT2 is nearly full with the CANOPY set (see above). Upstream's next steps (bind-time firmware
  addresses, runtime slimming, R12-8) all touch the runtime's size.
- [O] Upstream work that will meet the fork's code: runtime slimming C1 to C4 (the loader's commit path, which `publish_v2`
  uses), R12-8 (DSP dispatch; `core/instrument-play`), B1 (hook exits through `dk_tail`, `dk_exit`, `dk_ret`).
- [O] `gate-card:card` cannot pass in a worktree whose `out` is a link to drvfs (the daemon's control socket; port handover).
- Hardware: nothing installed.
