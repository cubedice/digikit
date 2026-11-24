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

Items 1 to 9 are done on `integrate/upstream-1124` (2026-11-24); item 10 is under way.

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
    PRISM, Overloop and RELAY; the hardware set's fit (`canopy-overloop-prism`).
