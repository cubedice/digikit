# Upstream merge of 2026-11-24 (upstream `c8f8a02c3`)

Phase B of [the image diet](../fork-image-diet.md): the fork (`feat/instrument-service`, `741d059c5`) takes upstream `main` at
`c8f8a02c3`, 1,019 commits past the merge base `92ac7fe4a`. Branch `integrate/upstream-1124`, tree `~/digikit-merge` on the WSL disk.
Nothing is pushed or installed.

The merge commit resolves every textual conflict; it does **not** build. The commits after it port what the list below names, then
regenerate, build and test. This file holds the list; each item is struck through when its commit lands.

## The layout

Upstream moved the tree: `modules/core/runtime` to `runtime/core`, the platform adapters to `runtime/platform`, the plug-in points to
`image-modules/points`, the services to `services/NAME`, `devices/fw` to `ports/fw`, the firmware bindings to `ports/bindings`, content
modules to `modules/<kind>/<author>/<name>`. The fork's own modules follow:

| Fork | Now |
| --- | --- |
| `modules/core/adapters/{copy-watch, fxview, instrument-play, instrument-registry, keyboard-panel, loop-control, loop-display, loop-storage, midi-events, pattern-position, phrase-control, phrase-ui, relay-input, relay-storage, relay-track, track-keyboard}` | `image-modules/adapters/NAME` (upstream's folder for image modules that are neither platform nor plug-in point) |
| `modules/core/services/{instrument, track-keyboard}` | `services/{instrument, track-keyboard}` |
| `modules/machines/canopy`, `modules/machines/relay` | `modules/machine/digikit/canopy`, `modules/machine/digikit/relay-machine` |
| `modules/fx/overloop`, `modules/fx/prism` | `modules/fx/digikit/overloop`, `modules/fx/digikit/prism` |

`dirmap.json` beside this file is the whole map; paths in the fork's files were rewritten with it (relative paths resolved against
the old location). Upstream's own lines were left as upstream wrote them.

## Decisions taken in the merge

- **Staging buffer.** The fork's per-device stage (`DKRT_STAGE` in `device.s`, `dkrt_stage_bytes`, `[limits] stage_bytes` per device)
  is gone: the fork's DT2 stage was already 128 KiB, upstream's fixed `STAGE_BYTES`. Upstream's versions of every file of that
  feature are taken (DN2 then stages 128 KiB too, as upstream).
- **Capacity kinds.** Upstream took kind 17 (`PACKAGE`); the fork's `INSTRUMENT_ROWS` and `INSTRUMENT_TRACKS` are 18 and 19. Their
  resources stay 7 and 8.
- **Patch sets.** The DIGIKIT menu is part of upstream's system package (`digikit/system.dksp` on the card), not an image module, and
  `core/lfo` is gone: both left the fork's patch sets. `overdrive-tracks` is gone as upstream removed it.
- **Services.** `[[service]]` rows `instrument` and `track-keyboard` follow upstream's `shortcuts` and `project` (eight provider
  services; upstream's `PROVIDER_MAX` is 8).
- **patchkit build.** The C and assembler sources of a Rust object (`include_dirs`, `-DDK_SERVICE=1`, `CSources`) are re-applied on
  upstream's builder, beside its cargo features: `rust::object_proto_with` and `rust::object_sha256` take the sources; upstream's
  `object_proto` and `source_sha256` keep their signatures.
- **System package block.** The fork's V2 instrument host (the coordinator, the loader's V2 path, the probes) is in the system package,
  which then needs 348,288 bytes: `PACKAGE_BYTES` is 348,288 (upstream: 327,680). The block is runtime bss in cf.expansion: with it
  the hardware set `canopy-overloop-prism` links with 10,272 bytes of cf.expansion free (container 493.1 KB below the stage). A separate
  host service does not fit (arena and stage), and a larger block (360,448) overflows cf.expansion.
- **DSP link window 4.** V2 registration data has window 4: the window field took bit 29 (the top bit of the chunk number), windows 0
  to 3 are bit for bit upstream's, window 3 stays upstream's statistics control, a transfer has at most 8,192 chunks (the most a device
  needs is 7,711). Older DSP code reads a window-4 chunk as a chunk number it never expects and drops it (`port/dsplink`, 4ad729a2e).
- **The other DT2 sets.** The block costs every image, not only the hardware set. Free cf.expansion with it: `device` 2,860, `emulator`
  18,828, `overloop` 62,532, `canopy-overloop` 42,384, `canopy-overloop-prism` 10,144. `phrase` (PRISM's tests) dropped
  `core/sample-pool` (about 55 KB; PRISM's tests load no samples): 54,096. `relay-track` was 13.5 KB short: the owner chose
  (2026-11-24) to shrink RELAY's waveform cache from one scan per slot (48 x 516 bytes) to `RELAY_WAVE_CACHED` = 16 entries, least
  recently used out (`relay-track/src/files.h`, `files.c`): 1,836 free. Its file calls `dkrt_image_file_*` moved from the package's
  `entry.rs` to the resident `storage/sdk_files.rs` (an image module links only resident symbols).
- **Resident table minor 5.** `instrument` appended at the end of the `links` group (`InstrumentLinks`, the image functions the host
  calls) and of the `views` group (`InstrumentViews`, the image's instrument words).

## To port (in this order)

Items 1 to 10 are done on `integrate/upstream-1124` (2026-11-24); nothing is pushed or installed on hardware.

1. Regenerate: `patchkit gen` (bindings, `dkrt.h`, `gen_*`), the port files (`tools/patchkit/port_resolve.py`), the objects. Done (4a75dd24a).
2. **Firmware anchors.** `devices/fw/project.toml` (fork: 88 lines: the pattern save, load, reset and track clear hooks of the track
   keyboard) was removed upstream as a file: port into `ports/fw/project.toml` (`devices_fw_project.toml.diff`). Check every fork
   `ports/fw/*.toml` the merge placed (`instrument-src`, `pattern`, `phrase-ui`, `relay`, `track-*`, `util`) against upstream's rule format. Done (4a75dd24a, cb743cdfb).
3. **patchkit.** Build it; fix what the merged fork features (`served_by`, INFO kind 3 ticks, keep-symbol of C statics, addrmap local
   symbols, `instrument.rs`, `dkmdsp.rs`, `link.rs`, `major3.rs`) need on upstream's code. `patchkit/src/bind.rs` lost the fork's two
   lines (bind is gone upstream: services go on the card packed with a port file). Done (4a75dd24a).
4. **Runtime.** The fork's V2 instrument host binding (`runtime/core/src/rs/src/instrument.rs`, `instrument/pending.rs`, `probe.rs`)
   and the loader's V2 path, the image-declared boot need and the image ticks were written against the fork's single runtime. Upstream
   split it into a resident core and the system package (`runtime/core/src/rs/src/package/`: the loader, planner and package manager
   load from the card). Port: `loader/mod.rs` (`boot_needs`, `publish_v2`, `admit_instruments`), `plan.rs`, `handlers.rs`,
   `report.rs`, `module_cmds.rs`, the loader tests (`*.diff` beside this file) into `package/loader`; `tick.rs` image ticks; decide
   which of `instrument` is resident (the policy bind on the first tick, the capacity the plan reads) and which is the package's. Done (cb743cdfb): the coordinator, the policy bind and the published words are the package's (`package/instrument.rs`, run in the LAST tick phase); the resident core keeps the instrument views (`instrument.rs`: words, file, session, load, binding) and the image ticks.
5. **DSP link window 3.** The fork sent V2 registration data in window 3 (`_dl_irows`, `_dl_iwords`); upstream uses window 3 for its
   stats control (`_dl_stctl`). The merge took upstream's. The window is two bits of the header word (bits 30-31): the V2 data needs a
   window of its own (a wider field, both CPUs, `test_dsplink.py`). CANOPY's DSP registration does not work until this lands. Done (port/dsplink, merged in 029b91a2a): window 4, above.
6. **machines service.** The fork's `MF_EXT` table (CANOPY's SRC pages through `src-pages.c`, the MOD routes through
   `instrument_mod_param`) in `hooks-dt2.s` and the fork's `image.rs`, `rt.rs`, `records.rs` changes (upstream deleted or rewrote the
   files) into upstream's machines owner. The fork's runtime-only exports `dk_machines_planned_ids` and `dk_machines_id_taken` (the V2
   loader's queries) are in `sdk/sdk.toml`; upstream has no runtime-only export of the machines file any more (its `Describe` part):
   keep them as internal exports or move them, and fix `packages/patchkit/tests/sdkgen.rs`. Done (port/machines, merged in cc3bafffc).
7. **fx service.** PRISM's send routes (`fxsend.rs`, 242 lines), the phrase rows (`src/phrase.rs`), Overloop's page painter
   (`DKRT_FXP_PAINT`, `loop_ui_paint`), the held values (`fxpage::held_setup`) and `fxpage.rs` into upstream's owner `Fx` (`Fx::start`
   binds the fxpages table). `services/fx/dks/src/hooks.rs` keeps the fork's `setup` for now. Then `docs/findings/65-fx-routes.md`. Done (port/fx, d7ad2d8fd, merged in 05d64d2b5): the fork's setup moved into `Fx::start`.
8. **The DIGIKIT menu** is in the system package: its weak `dk_app_instrument_module_settings` (a forwarder of `core/instrument-play`)
   must reach it through the package's import table (`package/menu_link.rs`) or a resident export. Done (e601a6064): `package/menu_link.rs` defines both through the `instrument` link rows.
9. **Tests.** Upstream puts services on the card packed, with the port file (`patchkit:ports`), instead of bound files
   (`dist/svc-bound`): the fork's `seedServices`, `track-card.ts`, `instrument-symbols.ts` and `loaded-service-symbols.ts` follow
   `card-seed.ts` and the service address maps of upstream. Remove `core/digikit-menu` and `core/lfo` from the fork's test records and
   module lists. `tools/nx` rig inputs of a coupled rig test (`importedModules`, Overloop with PRISM) if the rig tests need them. Done (333234376, 9e2d6698c).
10. Builds and gates: the host suites, patchkit, the runtime, the Python suites, then the device tests of the track keyboard, CANOPY,
    PRISM, Overloop and RELAY; the hardware set's fit (`canopy-overloop-prism`). Done: every DT2 set applies (fits above); the static checks,
    the Rust, host, assembly and Python suites pass (not `digiemu-desktop:rust-tests`: the WSL machine has no dbus, GTK or WebKit
    development packages); all 25 fork DT2 device test files pass in the emulator (track keyboard 11, CANOPY 7, PRISM 4, Overloop 2,
    RELAY 1). DN2 device tests and upstream's own device tests were not run here. What the gates found, beyond the port list:
    - **The key walker.** Upstream's shortcuts service hooks its entry (`cf.input.site.key_event_walk`) through the port file, which refuses
      a site an image module patched; core/fxview patched the same 8 bytes, so no card service loaded on a DT2 set with fxview. fxview takes
      the two instructions after the prologue now (`cf.page.site.phrase_key_walk`, `walk.s`); shortcuts run first.
    - **Boot needs.** Upstream's package manager brings up only the providers its roots need: the service of an image boot need
      (`boot_need_track_keyboard`) is a root of its own (`Loader::provider_is_boot_need`).
    - **Port file users.** The card's port file lists the declarations with a `core/services/` user: the two fork services are users of
      every name their packed files import (107), and two lockfile anchors got their missing declarations (`instrument_panel_trig_*`).
    - **The DSP link lease.** Upstream's blocking `link_run` (machines placeholders) spun in the UI task while the instrument host held the
      lease: `package::link::run` fails at once while the legacy side is held.
    - **Image file calls.** `dkrt_image_file_*` (RELAY's tape writes) are resident (`storage/sdk_files.rs`), not package exports.
    - **Tests.** Upstream boots live sessions from card images with the system package (`seededCard` of `live-card.ts` for the fork's seed
      folders), lists test image roots in `image-roots.json`, and starts a stock MAIN from its ready snapshot, made without a card (the
      track keyboard panel proof compares two cold boots of the card now).

## After the merge: alignment with upstream (asked 2026-11-24)

Four fork mechanisms were to move onto upstream's own (item 2), and the instrument host out of the system package (item 3). What
upstream `c8f8a02c3` has for each, read from its code and its plans:

| Fork mechanism | Upstream's | State |
| --- | --- | --- |
| Image boot need `boot_need_track_keyboard` (`plan.rs` `boot_needs`, `Loader::provider_is_boot_need` roots in `lifecycle/rt.rs`) | Selection pins: "a service path in the selection is a pin" (runtime-contract-and-lifecycle.md, decided 2026-11-22, RC2) | Not built upstream: its loader refuses a service file at a module index (`check_head`, `E_FORMAT`) and nothing pins a node. The boot need stays until RC2's pins land; then the card's selection names `services/track-keyboard.dks` and the three fork pieces go. |
| `dk_machines_planned_ids`, `dk_machines_id_taken` (runtime-only exports of the machines file) | `internal = true` exports of `sdk/sdk.toml` ("an export that only the runtime calls, by name, through the loader's export table"); the plans' direction for runtime-to-service bridges is Query and Describe (rc3-briefs.md, E1) | Kept: they already are upstream's internal exports. Query does not fit: `resident_query` answers only once the boot is closed, and the V2 admission asks during the boot's check of CANOPY (`admit_instruments`). |
| CANOPY rows of the DIGIKIT menu (`dk_app_instrument_settings_rows`, `dk_app_instrument_module_settings` through `package/menu_link.rs`) | No Settings part (Em, 2026-11-23: rejected); a service adds a SETTINGS row with `dk_ui_settings_row` in `init` (fx's FX ROUTING) | Open, for the owner: the two DIGIKIT rows (CANOPY, CANOPY STATUS) could become SETTINGS rows of `services/instrument.dks`; that moves them in the menu. A module's own settings page (CANOPY's) has no upstream mechanism before RC3m, so `dk_app_instrument_module_settings` stays. |
| The V2 lease (`dl_claim`, `dl_send_owned`, `link::legacy_held`) beside the link queue | `dkrt-link`'s `Link` has owners and a queue, no exclusive owner | Kept: one arbiter means an exclusive mode in upstream's `dkrt-link` crate, a change to propose upstream rather than carry. |

**Item 3, measured.** The fork's host is about 55 KB of the system package's 348,512 bytes: `package::instrument` 12,004 text, 4,366 bss,
2,736 data; the fork-only SDK modules `instrument_{coordinator, admission, load, mailbox, section, descriptor, provider, image, registry,
exchange, transport}` 31,772 text and 4,096 data; `dsp_memory` 1,014 (sections of `runtime/core/build/package.dko.json`). Out of the package,
`PACKAGE_BYTES` could go back to 327,680 and every DT2 set would gain 20,992 bytes of cf.expansion. The arena pays instead, on every CANOPY
boot. The CANOPY needs test (`needs.json` of its run) has the arena at 382,464 of 524,288 bytes with CANOPY, machines, instrument and the
port file; `patchkit needs` gives track-keyboard 66,208 and fx 52,784 more: about 449 KB with the track keyboard, 502 KB with fx too. With
the host in `services/instrument.dks` (about 55 KB more), CANOPY with the track keyboard and fx would need about 557 KB: it would not load.
The host stays in the package unless the owner gives up that combination or the arena grows (which costs cf.expansion again).

**The area gate (item 6).** The merge left the fork's paths outside the merge gate's areas (`tools/nx/area-paths.mjs`). The two fork
services became users of 107 port declarations, and no record carried their tags: an edit of 13 port files, of which 7 have only fork users
otherwise (`draw`, `instrument-src`, `text`, `track-keys`, `track-sequence`, `track-steps`, `track-trig`), ran every affected device test. And
the instrument host had no owner in `runtime/core/owners.toml`: the record of `core/runtime` covers its own tree and lists no test file, so an
edit of `package/instrument.rs` or the instrument SDK modules selected no device test at all. Now `S:instrument` and `S:track-keyboard` are
in the tag vocabulary (`tools/nx/acceptance-tags.mjs`), the fork's records carry them and the adapters they prove (`A:core/fxview`,
`A:core/relay-input`), `S:instrument` owns the host (`dkrt::instrument`, `dkrt::package::instrument`, the `dkrt_sdk::instrument_*` modules), and
the CANOPY needs test has `C:loading` (the loader's V2 path). An edit of each of those paths now selects the fork tests that load it
(simulated with `areaSelection` of `tools/nx/test-subset.mjs`). Upstream has the same hole for its own unowned package modules
(`dkrt::package::menu`, `menu_link`, `image`, `host_dir`): an edit there selects no device test either.

## RELAY on the hardware image (branch `feat/hw-relay-fit`)

The owner runs CANOPY, Overloop, PRISM, the track keyboard and RELAY together on the Digitakt II. After the merge, `core/relay-track` (35,184
bytes of cf.expansion with relay-input, relay-storage and plusdrive-write) did not fit beside `canopy-overloop-prism`: the image was about
23 KB over. The patch set `canopy-overloop-prism-relay` now holds all five. What made the room:

- **A V2 file keeps its DSP section on the card.** The loader copied CANOPY's whole DSP section into its arena block and kept it for as long as
  CANOPY was loaded, beside the relocated output. The owner of the instruments now stages the same store file again when its transaction
  starts (the load checks the file against the hash of its store entry, so a file replaced since the boot is refused) and admits the section
  from the staging buffer. `canopy.dkm` needs 87,056 bytes of arena (189,008 before); `dkneeds::needs_of_v2` counts the same block.
- **Runtime buffers** (`runtime/core/abi.toml`, the device files' `[limits]`):

  | Buffer | Upstream | The fork |
  | --- | ---: | ---: |
  | `ARENA_BYTES` | 524,288 | 491,520 (409,600 since the streamed upload below) |
  | `PACKAGE_BYTES` | 327,680 | 352,768 (349,088 used; 349,920 with the streamed upload) |
  | `STAGE_BYTES` | 131,072 | 106,496 (the largest staged file is `canopy.dkm`, 102,344 bytes) |

  `max_chunk` (32 KiB) and `TBL_CAP` (8 KiB) are upstream's: uploads keep their speed.

Free cf.expansion on DT2: `canopy-overloop-prism-relay` 18,548, `relay-track` 54,684, `device` 55,724. With all five modules, the services
and the track keyboard service loaded, the arena has 403,280 of 491,520 bytes in use (75,408 free in one piece). Tried and dropped on the
way: a smaller upload chunk and table buffers (slower uploads), and an arena allocator that placed DSP transfer copies at the top (it broke
the full-set capacity test and is not needed once CANOPY's section stays on the card).

## The DSP image streamed from the card (branch `feat/v2-stream`)

After RELAY fitted, the largest thing left in CANOPY's arena block was the relocated copy of its DSP image (86,720 bytes), kept for as
long as CANOPY was loaded so that the upload could read it. The owner of the instruments still admits the section from the staging
buffer, but keeps only what relocating needs (the base and the resolved imports, with every fixup checked as before) and sends the image
from the file: each 4 KB chunk is read with the store's sector reads, relocated, and written to the link (`dkrt_sdk::instrument_stream`,
`Transfer::begin_streamed`, `Coordinator::admit_streamed`).

- **The admitted bytes, or nothing.** The stream hashes the image and the relocation table as it reads them and compares the hashes with
  those of the admitted section; a fixup that a chunk boundary cuts must read the same from both chunks. A file that changed after the
  admission, a failed card read or a table out of order stops the upload before the BSS is cleared or anything is initialized, and the
  load detaches as after a link failure (`instrument_image::Error::Source`). The packer writes the relocation table in offset order
  (`dkmdsp.rs`), which the streamed admission requires (`RelocateError::Order`).
- **The V2 block** holds the module, its keys table and a fixed upload buffer of 5,120 bytes (one chunk and two sector buffers,
  `dkneeds::instrument::UPLOAD_BUFFER`). With the five modules loaded the arena has 321,680 bytes in use (403,280 before): 81,600 less.
- **`ARENA_BYTES` 409,600** (400 KiB, 80 KiB less): the hardware set's peak drops by the same 81,600 bytes, so the arena keeps the
  headroom it had at 480 KiB, and the 81,920 bytes go to the image.
- **The cost**: about 200 single-sector card reads per CANOPY load, made between the link writes of the upload (the relocated copy at
  admission is gone). Not measured on hardware yet.

Free cf.expansion on DT2: `canopy-overloop-prism-relay` 100,452 (18,532 before). PACKAGE 349,920 of 352,768 bytes. The arena with the five modules: 321,680 of 409,600 in use, 75,088 free in one piece. With the owner's settings record and
selection, the rehearsal boots all five modules, CANOPY's instrument becomes ready, and the ten settings entries read back unchanged.

The instrument service's tables (`services/instrument.dks`, 47,834 bytes of bss) were checked for the same kind of slack and have none
that a smaller constant would remove: the MIDI ledger (12,408: 128 events of 76 bytes, 128 held keys, 33 routes), the four instances'
state (9,624, most of it a 128-key ledger per instance), the status queue (6,656: 128 replies, sized for the UI's 150 ms window at 750
replies a second), the shared-note queues (5,400: 20 notes per instance, one release reserved for each). What would save space is a
change of structure: one key pool shared by the four instances (about 6 KB) or the lock and source snapshots of an event in a side table
for the sequencer's notes only (about 4 KB). Not done.

The track keyboard service has key tables of its own (a 512-key MIDI table, 48 keys per track) for the tracks it routes, and the
image's MIDI router asks it first: on a channel it routes to a CANOPY track, CANOPY's ledger takes no new note and only releases the
keys it already held. The ledger is not a second copy, though: it also carries the panel keys, the sequencer's trigs with their lock
snapshots and CANOPY's own channel routes (the owner's `canopy/routes/v1`), and it is CANOPY's MIDI input when `track-keyboard.dks` is
not loaded. The per-instance key state is CANOPY's chord and voice layer, which notes from either path go through. Making the track
keyboard CANOPY's only external MIDI input would save at most the 33 routes and part of the 128-key pool (under 2.6 KB), and would
need the saved routes moved to the track keyboard's.

## RELAY as a service file (branch `feat/relay-service`, owner's request)

RELAY's ColdFire code (`control.c` and `files.c` of `core/relay-track`: the tape transport and its commands, the SRC pages and
waveforms, the tape files, the Sound association of tapes) moved out of the image into `services/relay.dks`
(`core/services/relay`), the way the instrument and track keyboard services did (docs/plans/fork-image-diet.md sections 9 and 9.2).

- **What stays in the image** (`core/relay-track`): its DSP objects (SHARC hook sites cannot move), its six parameter entries, the
  seam, and `image.c`, the two block-interrupt paths that run without the service: `core/dsp-link` reserves RELAY's frame words whenever
  the adapter is in the image, so `relay_idle_frame` writes the timing words and no command, and `rtm_gate_pass` leaves the stock frame
  as it is. `core/plusdrive-write` stays an image module: it is the runtime's +Drive write plug-in, not RELAY's.
- **One slot, `relay_vec`, 11 forwarders** (`services/relay/seam.toml`): the UI pass of `core/loop-control`; the reply, the frame and
  the stock-frame gate of `core/dsp-link`; RELAY's type check, MOD routes, SRC page and Sound save check, which `core/track-keyboard` and
  `core/instrument-play` call; the stop key and the picker of `core/fxview`. Without the file each answers what its caller took for a
  missing function, so the DT2 runs without RELAY.
- **The image table `relay_image`** (37 words): 26 image functions (the runtime's file, I/O and interrupt calls, `core/ui`,
  `core/fxview`, the Sound association and SRC page of `core/instrument-play`, the SRC clock, `core/machines`, `core/pattern-position`),
  2 data words (`machines_info`, `pp_position`) and the 6 registry codes of the MOD destinations. The C reads them through
  `relay-seam.h` under `DK_SERVICE`; the host tests that compile it are unchanged.
- **The need**: `core/relay-track` is `served_by = "relay"`, so RELAY's machine module needs `relay.dks` (with `machines.dks` and
  `instrument.dks`) and the planner refuses it without the file. The firmware names the file binds list `core/services/relay` as a user
  in `ports/fw`, so the card's port file declares them.

Measured [D] (DT2 1.17):

| | Before | With `relay.dks` |
| --- | ---: | ---: |
| `core/relay-track` in cf.expansion | 34,632 | about 460 (forwarders, table, idle paths) |
| free cf.expansion of `canopy-overloop-prism-relay` (400 KiB arena) | 100,452 | 135,132 |
| arena with the five modules (rehearsal) | 321,680 | 359,408 (`relay.dks` needs 37,568; file 29,664 bytes) |

The move frees image space only while RELAY is not selected: with all five modules the pool holds the same code, plus about 2.9 KB of
service glue. `ARENA_BYTES` is 450,560 (440 KiB) so that the hardware set keeps the arena headroom it had: with the five modules 359,408 of 450,560 bytes in use and 78,320 free in one piece (75,088 at 400 KiB with RELAY in the image), and the ten settings entries read back unchanged. Free cf.expansion
of the DT2 sets with it: `canopy-overloop-prism-relay` 94,172, `relay-track` 130,320, `device` 96,668.

## CANOPY as a voice host (branch `feat/canopy-voice-host`, owner's choice)

The track keyboard is the note engine of every track. CANOPY's host kept its own: a keyboard mapper (PLAY, ROOT + TYPE, LEARN, a
physical key ledger), CHORD LEARN with a shape library, its own MIDI route (`canopy/routes/v1`, `canopy/channels/v1`) and its own KEYS
and ARP pages for firmware without the track keyboard. The owner chose to keep CANOPY's voices (four in one track, the arp lane and the
release tails, mixed into the track's chain) and to make the track keyboard CANOPY's only note engine.

Decisions (owner, 2026-11-25):

- Switch every CANOPY track to the track keyboard, measure on the owner's own Sounds, and report before anything is removed.
- CANOPY's MIDI comes from the Digitakt's own MIDI config (the track keyboard's routes). CANOPY's route table is retired; its records
  stay on the card, untouched. The owner's route is channel 2: a controller on channel 2 needs track 1 on channel 2 in the Digitakt's
  MIDI config.

The track keyboard now takes an unedited CANOPY track too, with the performance settings its Sound carries (8d81b38f5); a pattern
that locks a step to a Sound that is not CANOPY's still gives the track back.

**Parity on the owner's Sound** [D] (`modules/machine/digikit/canopy/tests/engine-parity.manual.device.test.ts`, DT2 1.17 emulator
with the SHARC core, the owner's settings record from the device backup, two runs; track 1 held by the track keyboard in every capture
of the shared image: claim 1, lanes 3):

| input | CANOPY's own engine | track keyboard |
| --- | --- | --- |
| sequencer, four trigs for two bars | peak -17.2 dB, release 2.08 s | -16.6 dB, 2.12 s; the same pitches, envelope within about 1 dB |
| panel chord, three keys for 2 s | -29.1 dB, 2.28 s | the same notes, 2.2-2.5 dB quieter, release 0.1-0.36 s longer |
| USB MIDI chord, C-E-G | silent 0.85 s after the first note while the keys are held | holds to the note-off, release 2.35 s |

The owner accepted the panel difference. CANOPY's own MIDI cut-off does not happen on the device (owner, 2026-11-25): it is the
emulator's, and the track keyboard's MIDI behaviour is the one kept.

The harness writes the owner's record into a copy of the seeded card. The daemon restores a booted state when the image and the card's
identity match, and the identity is the card's content id (`card.key`), its length and its overlay, not its bytes: the copy gets an id
of its own (the base id and the record's hash, as `linkCard` in `tests/regression/runtime/write-safety.ts` does). With the base id
copied, a boot of the base card (no record) was restored, and the first change of a key wrote a fresh record over the owner's in the
copy; that was the harness, not the runtime.
