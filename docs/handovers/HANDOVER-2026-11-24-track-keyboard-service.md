# Handover 2026-11-24: the track keyboard's player as its own service file

DT2 OS 1.17, WSL emulator. It follows [the 2026-11-23 handover](HANDOVER-2026-11-23-instrument-service.md) and
[the plan](../plans/fork-image-diet.md), section 9.2. Everything below is from this session's builds and runs unless it says
otherwise. Nothing was installed on hardware, nothing is pushed. Branch `feat/instrument-service`, tree `~/digikit-diet` on the WSL
disk, scripts and logs in `/home/mdaven/digikit-data/out/diet/` as before.

The owner asked for the track keyboard's code to move to its own service file, so that the hardware set keeps room after the
upstream merge.

| Commit | What |
| --- | --- |
| COMMIT1 | The seam generator writes every `seam.toml`; forwarders and thunks jump through `a1` (the boot fault below). |
| COMMIT2 | `services/track-keyboard.dks`: the player of `core/track-keyboard` in a service file, the image keeps the pattern storage and the hook sites. |

## Numbers [D]

| | `cf.expansion` used of 1,396,736 | free |
| --- | ---: | ---: |
| `canopy-overloop-prism` with the track keyboard in the image | 1,279,340 | 117,396 |
| the same with its player in `services/track-keyboard.dks` | 1,216,624 | 180,112 |

- `services/track-keyboard.dks` (dt2): 81,416 bytes packed (image 60,296, bss 5,036), 78,616 bound; arena need 65,360. It needs
  the runtime (and the UI service, optionally): no service of CANOPY's.
- The files of the hardware set's card (CANOPY, Overloop, PRISM, remote, `machines.dks`, `fx.dks`, `instrument.dks`,
  `track-keyboard.dks`) need 469,904 of the 524,288-byte arena.

## Proofs (emulator, DT2 1.17)

- The eleven registered device tests of the track keyboard pass in the emulator: ten on an image without `core/instrument-play`
  with `services/track-keyboard.dks` on the card, the CANOPY one with both service files. Two passed on a second run: `midi` first
  failed in its Nx dependency `core/runtime:test-assembly`, which read `dist/bytes/images` while a second list of targets running at
  the same time rebuilt it; `pool` first read a static by the section name the image's map had given it (the test is fixed).
- With the forwarders on `a1`: CANOPY's seven device tests, PRISM coexistence and RELAY's two pass.
- A probe, not kept as a test: the image of `canopy-overloop-prism` with all eight files on its card (CANOPY, Overloop, PRISM,
  remote, `machines.dks`, `instrument.dks`, `fx.dks`, `track-keyboard.dks`) loads every one of them. The same image without
  `track-keyboard.dks` boots, with stock TRIG pages.
- Host: the four test files that compile the track keyboard's C (5 tests); the runtime crate's 381 (with `-` written `_`); the seam
  checks of both service crates; `test_track_keyboard_image.py` (the image's tick and boot need, the forwarders, the file's needs,
  the hardware set's room), the instrument seam test and CANOPY's need test.
- Static: `patchkit gen --check`, `patchkit build --check`, the seam `--check`, `patchkit service-check` of both crates, the layer
  check (no finding in the changed sources), ruff, prettier; `tsc` gives the same eight errors as before the change.

## Found on the way

- **The forwarders and thunks jumped through `a0`**, which this GCC uses for the address of a returned struct: the first call of
  `tk_defaults()` through its thunk wrote the struct over the image function, the next one executed it (boot fault, vector 4).
  Found by keeping the emulator's `boot-fault.json` (it removes the staging dir of a failed cold boot; an `LD_PRELOAD` shim that
  skips unlinking `boot-fault*`, `out/diet/keepfault.so`, kept it) and naming its addresses with the image's address map. Both
  seams use `a1` now; no function of the instrument seam returns a struct.
- **`core/midi-events` was `served_by = "instrument"`**, which gave the track keyboard's file a hard need on `instrument.dks`. Its
  moved code is CANOPY's MIDI ownership, reached only by users of `core/instrument-play`, which keep the need; the rule is off.
- **An info name is `[a-z][a-z0-9_]*`**: the runtime writes each `-` of a service's name as `_` in `boot_need_<service>`.
- **`PROVIDER_MAX` is 6**, and there are six provider services now (samples, machines, fx, lfo, instrument, track-keyboard).
- The manual runner (`manual.sh`) does not rebuild `dist/svc-bound`: run `nx run-many -t bind` first, or use the Nx targets.

## Open

- Phase B, the upstream merge (plan section 8 lists what it must port, now with the track keyboard's service).
- `PROVIDER_MAX`: a seventh service file would need a larger table.
- `auto-pool` stays manual (not run here).
- Hardware: an install needs `services/track-keyboard.dks` and `services/instrument.dks` on the card with the new image, and the
  owner's go.
