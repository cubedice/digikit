# Fork image diet: the instrument code to a card service, CANOPY to a 128 KiB stage

Date: 2026-11-23. Status: **Phase A stopped after step 1, the measure.** The brief (move the instrument code out of
`cf.expansion` and fit CANOPY's package into a 128 KiB stage, on the fork's base, before the upstream merge) estimates at
8 to 10 agent days, past the five days its stop rule allows (section 7). Nothing was built: WSL did not start in this
session (`wsl --status` hung for more than 20 minutes), so the numbers are static [D], from the committed objects at
`3a3a91270`, calibrated against the last built image. [Finding 372](../findings/372-fork-image-budget.md) holds the
measurement; this file holds the plan and the options. DT2 OS 1.17 only, as the fork's instrument code.

## 1. Why

Upstream main (runtime 0.11.x, item RC2s of its `runtime-contract-and-lifecycle.md`) keeps a resident core in the image
and loads the loader, planner, package manager and DIGIKIT menu from the card as `digikit/system.dksp`. The package has
its own block in the runtime's bss (`PACKAGE_BYTES` = 327,680), the arena stays 524,288 bytes on every device, and the
stage is 131,072 bytes (`STAGE_BYTES` in upstream's `runtime/core/abi.toml`). The owner's static estimate for the fork's
DT2 hardware set after the merge: about 57 KB over `cf.expansion` (1,396,736 bytes) with a 128 KiB stage, about 77 KB
over with the fork's 148 KiB stage. The fork-only image adapters of that set are 166,305 bytes, of which
`core/instrument-play` and `core/midi-events` are 137,225 (section 2).

## 2. The budget today

The set is `canopy-overloop-prism` (`devices/digitakt-ii.toml`). The installed image is that set:
`firmware/hardware/2026-11-20-looptrans`, image `9ee1ed898e39214e`, commit `7369a18f5`, before RELAY (PR 10) and the
track keyboard (PR 11). It uses 1,318,652 of 1,396,736 bytes of `cf.expansion` (78,084 free) and 566,460 of 922,756 of
`dsp.l2`. RELAY's own set is `relay-track` (`core/relay-track`, the menu, health and input): no CANOPY, PRISM or
Overloop pages. The sum of the committed object sizes of `canopy-overloop-prism` at `3a3a91270` is 1,315,003 bytes; the
build adds about 3.6 KB (tails, stubs, alignment), so the head is about where `9ee1ed898e39214e` was.

| Object (fork only, `cf.expansion`) | Bytes | text | rodata | data | bss |
| --- | ---: | ---: | ---: | ---: | ---: |
| `core/instrument-play` `play-cf` | 102,837 | 66,682 | 8,034 | 80 | 28,041 |
| `core/midi-events` `midi-events-cf` | 34,388 | 12,770 | 0 | 0 | 21,618 |
| `core/phrase-ui` | 9,412 | 8,188 | 424 | 0 | 800 |
| `core/phrase-control` | 7,220 | 6,108 | 84 | 32 | 996 |
| `core/loop-control` | 4,334 | 2,334 | 0 | 8 | 1,992 |
| `core/loop-display` | 3,893 | 3,466 | 159 | 0 | 268 |
| `core/pattern-position` | 3,144 | 2,508 | 28 | 0 | 608 |
| `core/fxview` | 1,077 | 1,074 | 3 | 0 | 0 |
| **Sum** | **166,305** | | | | |
| Not in the set: `core/track-keyboard` | 77,514 | 69,684 | 2,234 | 9 | 5,587 |
| Not in the set: `core/relay-track` | 51,513 | 16,890 | 626 | 35 | 33,962 |
| Not in the set: `core/plusdrive-write` (RELAY needs it) | 8,501 | 6,536 | 86 | 0 | 1,879 |

The set with RELAY and the track keyboard (`canopy-overloop-prism` plus `core/relay-track` and `core/track-keyboard`) sums
to 1,452,531 bytes: about 59 KB over at the 512 KiB arena today. `docs/design/dt2-track-arpeggiator.md` builds it only
with a 436 KiB arena.

## 3. What can move and what must stay

The candidates are the ColdFire objects of `core/instrument-play` and `core/midi-events` (137,225 bytes). Their SHARC
objects stay (`play-dsp` 20,640 bytes, `midi-packet-dsp` 3,884, in `dsp.l2`): SHARC hook sites cannot move
([finding 128](../findings/128-r4-hooks-in-services.md)), and `dsp.l2` is not the limit.

How the firmware and the image reach the code (from the relocations of the committed objects):

| Entry | Who calls it | Context | In the plan |
| --- | --- | --- | --- |
| 15 hook sites of `core/instrument-play`: `instrument_lock_due_1`, `_2`, `instrument_mod_split`, `_recombine`, `instrument_panel_on`, `_off`, `instrument_note_list_write`, `sound_deserialize`, `sound_serialize`, `sound_init_slices`, `sound_type_write`, `selection_build`, `memcpy`, `memset`, `bzero` | stock firmware | UI and project tasks; the lock hooks in the sequencer path; `memcpy`, `memset`, `bzero` everywhere, interrupts included | the first twelve become hooks of the service, installed at load (as `machines.dks`, and upstream's `lfo.dks`, which installs interrupt hooks); `memcpy`, `memset` and `bzero` stay image hooks with a stub, because a task preempted inside their six-byte site during the install would resume inside the new `jmp.l` (finding 128 [O]) |
| 2 hook sites of `core/midi-events`: `din_rx`, `dsp_reset` | stock firmware | the DIN router task; the boot task's DSP reset | image hooks with a stub: `dsp_reset` runs at boot, before the service can load |
| 11 vtable entries and 2 call targets (`instrument_src_*`, `instrument_panel_*`) | stock firmware | UI task | patchkit writes data at build, and the installer writes only `jmp.l` over code and slot words: the image keeps a stub per entry that jumps to the service or to the stock handler |
| `instrument_write_frame`, `instrument_write_lock_frame` | `core/dsp-link` `dl_tick` | the block interrupt | image stub through the vector (below) |
| `instrument_play_tick`, `_faulted`, `instrument_runtime_policy`, `mi_registry_reply`, `mi_registry_expect_session` | runtime | UI task, boot plan | image stubs with an "absent" result |
| `dk_app_instrument_settings_rows`, `dk_app_instrument_module_settings` | `core/digikit-menu` | UI task | image stubs |
| `mi_channel_message` | `core/platform` | MIDI receive task | image stub |
| `instrument_mod_param` | `core/machines` (`machines_ext_info`) | UI task | image stub |
| 9 functions (sound association, SRC drawing) | `core/relay-track` | UI task | image stubs |
| 9 functions (lanes, locks, ownership) | `core/track-keyboard` | UI task and block interrupt | image stubs |

The code also calls out of itself: `core/ui` (14 functions, all SDK imports a service can link), the runtime (5 plus the
interrupt mask pair), `core/pattern-position` (2), `core/dsp-link` (2), `core/machines` (2), `core/track-keyboard`
(13 optional), `core/relay-track` (4 optional) and about 90 firmware anchors and hook returns. A service names firmware
through `patchkit bind`; the image functions it needs go into one `[[info]]` table of the image adapter.

**Interrupt context.** The block-interrupt closure of the two frame writers is 50,872 bytes: 12,862 of code and 37,946
of bss (`mi_ledger` 12,408, `inst` 9,624, `status_words` 6,144, `shared_notes` 5,400, ...). That bss is the state the
UI task reads and writes too. Kept in the image, it leaves a saving of about 86 KB, and every task-side access of the
moved code would cross the boundary by pointer. Moved, the interrupt reaches it through the vector: the fork does this
already (`machines.dks` fills the DN2 frame-type interrupt slot, `mf_frametype`), and so does upstream (`lfo.dks`).
So the plan moves it. What a service cannot provide is code that must run before it is loaded: the stubs take the stock
path, or return "absent", while the vector is zero.

No firmware hook is new. The only declaration change: the tails of the `before` hooks the service takes
(`selection_build`) must be declared `tail = true` in `devices/fw` for `patchkit bind`.

## 4. The design

**Service `core/services/instrument`, file `services/instrument.dks`.** The ColdFire C and assembler of
`core/instrument-play` and `core/midi-events`, with a Rust glue crate (`dkrt_svc_rt::service!`: no record kinds, the
twelve hooks of section 3, one fill, no exports). Image about 90 KB, at most about 2,950 relocations (the format allows
4,096), at most about 117 KB as a file: under the 128 KiB stage. bss about 50 KB in the arena. The twelve hooks bring the
installer to 42 of its 48 sites (`install_sites`: machines 21, fx 9).

**Image adapter `core/instrument-play`** (the id stays, so `core/relay-track`, `core/track-keyboard` and CANOPY keep
their `requires`): the DSP objects, one `[[slot]]` word (`instrument_vec`, zero at build), one stub per entry of
section 3 (about 47 entries at 16 to 24 bytes) and one `[[info]]` table (`instrument_image`: a version word, the
addresses of the image functions the service calls and the image tails of the hooks it keeps). The service's fill writes
the address of its vector table into the slot after its setup has checked the table's version; a stub with a zero
vector takes the stock path or returns "absent". `core/midi-events` becomes a DSP-only adapter. Image cost about 1.5 KB.

**Refusal.** The packer adds a hard NEED on `instrument` to a module with a V2 instrument section (the rule
`record_kind_needs` follows for record kinds), so the planner refuses CANOPY when the service is missing or refused
(wrong firmware: `E_HOOK_STOCK`; wrong version: `E_CONTRACT`), and the rest of the selection loads.

**Toolchain gap.** `patchkit pack` takes one ColdFire object per file, and a service object is a Rust crate. The C and
assembler must be linked into the crate's `ld -r` (`packages/patchkit/src/build/rust.rs`), compiled with the ColdFire C
flags, with every global but `dk_module` and `dk_svc_abi` made local (`patchkit service-check`), and the Nx inputs of
the object must hash the C sources.

**Phase B.** Upstream has the same pieces (`[[fill]]` slots, `[[info]]` tables, hooks installed at load, `service!`).
Phase B moves the service crate to `services/instrument`, replaces the glue by upstream's `service!` or its RC3 provider
macro (owner object, tagged parts), registers the twelve hooks with upstream's RC4 installer, and keeps the image adapter
as a fork adapter beside upstream's `runtime/platform`. The vector table, the info table and the C sources do not
change. The NEED rule must be ported to upstream's packer, or replaced by a provider-qualified resource (RC3:
`instrument.rows` owned by the service).

## 5. CANOPY's package and the 128 KiB stage

`canopy.dkm` is 147,768 bytes (install record `2026-11-20-looptrans`); 131,072 needs 16,696 fewer. Its DSP data holds the
five factory wavetables, 92,720 bytes (23,180 words, `src/dsp/wavetables.s`), and the FM tables, 4,196 bytes.

| Option | Package | Cost | Notes |
| --- | ---: | --- | --- |
| **A. Half tables.** Every one of the 280 mip levels is exactly antisymmetric after quantization: `w[N-n] = -w[n]`, and `w[0]`, `w[N/2]`, `w[N]` are 0 (checked against the generator; the committed assembly equals it). The package holds 22,760 samples (45,520 bytes); a DSP routine expands them into a bss table (the DSP section format reserves bss without sending it) before the first render | about 100.6 KB | 1 to 1.5 days | bit-exact, so the PCM pins stay; nothing new on the card or in the runtime; the expansion (about 23,000 words) runs once, spread over the first process calls; a rig test compares the expanded table with the generator |
| B. Data file `data/digikit/canopy/wavetables.bin`, loaded into a DSP bss table | about 55 KB | 1.5 to 2 days | needs a new loader item (read a card file in stage-sized pieces into a module's DSP bss before admission): format, runtime, packer, host and assembly tests; Phase B ports it to upstream's loader |
| C. Generate the tables on the DSP from the coefficients | about 55 KB | 1 day | not bit-exact (double against SHARC float): a change of sound, which goes to Em |

Recommended: A.

## 6. Steps

| Step | What | Proof | Days |
| --- | --- | --- | ---: |
| 1 | Measure on a built image (apply, plan and addrmap of both sets) | the numbers of section 2 from the image | 0.25 |
| 2 | patchkit: C and assembler in a service object; service-check, layer check, sdk registration of the service, Nx inputs | patchkit unit tests; `patchkit:objects-check`; `gen --check` | 1 |
| 3 | The service and the image adapter: move the sources, the vector, the info table, the stubs, the twelve load-time hooks, the version checks | host tests of the seam (lookup, refusal when absent, version); assembly test with the real bound file | 2 |
| 4 | The NEED rule; the device case "boot without `instrument.dks`": CANOPY refused, the rest loads | packer unit test; device test on DT2 | 0.5 |
| 5 | The tests of the moved code: about 100 files outside `docs/` name its source or object paths (47 host tests in `tests/regression/host`, 10 Python files in `tests/patchkit`, about 20 fixtures, and `core/track-keyboard` and `core/relay-track` include its headers). Leaving the sources where they are, named by the service's object, keeps them unchanged; moving them costs about another day. CANOPY's Python and host suites, the rig suites of CANOPY, PRISM, Overloop and RELAY | the suites pass unchanged | 1 |
| 6 | CANOPY half tables (section 5, option A) | rig: table equal to the generator, PCM pins unchanged; CANOPY's tests | 1.25 |
| 7 | Device subset on DT2 and DN2 (shared inputs: patchkit, the runtime's service table), with gate-smoke and gate-pins: S:machines, the instrument adapters, CANOPY, the track keyboard, PRISM, Overloop, RELAY, transport, loading; fixes | the subset | 2 |
| 8 | Hardware set build (apply, preflight, verify, pack-all, services), finding, READMEs, squash | `cf.expansion` and `dsp.l2` before and after, image id, card list | 0.5 |
| | **Total** | | **8.5** (8 to 10) |

The device runs are the critical path: each subset run takes one to three hours here, and the twelve load-time hooks
and the interrupt-context vector have only device proofs.

## 7. The stop, the options and the recommendation

The brief stops the work when it clearly runs past about five agent days. Section 6 gives 8 to 10. The other two stop
rules do not apply: the move needs no new firmware hook, and the saving without moving interrupt-context code is about
86 KB (over 80 KB), though invasive (section 3).

Saving and the Phase B arithmetic. Moving both objects saves about 135.7 KB (137,225 less about 1.5 KB of stubs):

| Set after the merge (owner's estimate) | Over by, without the move | Free, with the move |
| --- | ---: | ---: |
| `canopy-overloop-prism`, 128 KiB stage | 57 KB | about 79 KB |
| `canopy-overloop-prism`, 148 KiB stage | 77 KB | about 59 KB |
| with RELAY and the track keyboard (+137.5 KB), 128 KiB stage | 194.5 KB | over by about 59 KB |

So the move clears the brief's target of 80 KB (it covers the 77 KB worst case with about 59 KB to spare), but the set
with RELAY and the track keyboard still does not fit after the merge. `core/track-keyboard` (77,514 bytes) would have to
move too; it calls and is called by the instrument code, and with it one file would be about 200 KB with about 5,400
relocations, over the stage and the format's 4,096, so it would be a second service file (2 to 3 days more).

| Option | What | Cost | Result |
| --- | --- | --- | --- |
| 1 | The brief as written: steps 1 to 8 | 8 to 10 days | `canopy-overloop-prism` fits after the merge with about 79 KB free; RELAY and the track keyboard do not |
| 2 | Step 6 first, alone (CANOPY half tables) | 1 to 1.5 days | CANOPY at about 100.6 KB, so the stage can be 128 KiB now and after the merge; with `stage_bytes` at 131,072 that is 20,480 bytes of `cf.expansion` (the stage is runtime bss) |
| 3 | Option 2, then the merge with the arena at 436 KiB (the fork's track-keyboard recipe; upstream's `ARENA_BYTES` is one value for every device), then the move once, on upstream's service ABI (RC3, RC4), and the arena back to 512 KiB | 1.5 days, then the merge, then 5 to 7 days | no port of a fork-only service; the merged image runs with a smaller arena until the move lands |
| 4 | bss to the arena: the 49,659 bytes of bss of the two objects allocated at the first admission | 1 to 1.5 days | about 50 KB; does not reach 80 KB alone |

Recommendation: option 2 now, then option 3. Step 6 is needed on every route, is independent and cheap. Doing the move
after the merge, on the ABI it will live on, saves the Phase B port of a fork-only service (1 to 2 days) and lets the
move use upstream's installer and provider contract as they land. If the owner needs RELAY and the track keyboard on
the hardware image after the merge, plan the track keyboard's own service file with it.

## 8. What Phase B must port (if option 1 is built first)

- The service crate and its glue (section 4, "Phase B").
- The NEED rule for V2 instrument sections, or an `instrument.rows` resource owned by the service.
- patchkit's C sources in a service object.
- The `tail = true` declarations in `devices/fw` (upstream: `ports/fw`).
- The CANOPY half tables need nothing: they live in the module.
