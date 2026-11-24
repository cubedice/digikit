# Handover 2026-11-23: the instrument code as a card service, and the track keyboard on its own

DT2 OS 1.17, WSL emulator. It follows [the plan](../plans/fork-image-diet.md) (section 9 is the as-built design, 9.1 the track
keyboard) and [finding 372](../findings/372-fork-image-budget.md). Everything below is from this session's builds and runs unless it
says otherwise. Nothing was installed on hardware, nothing is pushed.

The owner chose option 1 of the plan (the full move before the upstream merge), then asked for the track keyboard to be a normal part
of the image that does not depend on CANOPY. Branch `feat/instrument-service`, cut from `rework/image-diet` (`5936100a1`). The build
tree is `~/digikit-diet` on the WSL disk; scripts and logs are in `/home/mdaven/digikit-data/out/diet/` (`env.sh` to source;
`devtest.sh TARGET LOG` one Nx device target, `devtest-list.sh SUMMARY TARGET...` several, `manual.sh FILE LOG` one test file;
`devtest-base.sh` and `manual-base.sh` the same in the baseline tree `~/digikit-svcc`, which is the base plus patchkit-only changes).

| Commit | What |
| --- | --- |
| 4b0a1e411 | The split: image part of `core/instrument-play` and `core/midi-events`, forwarders, `seam.toml` and its generator, the service's sources. |
| e957aa16f | `served_by` and the NEED rule in patchkit; `instrument_runtime_policy` stays in the image; the seam test. |
| b6e346ff6 | Plan section 9 and the built numbers in finding 372. |
| 43f3385e5 | The device-test records of CANOPY, RELAY and PRISM load `core/services/instrument`. |
| 9f9978853 | Merge of `feat/svc-c-sources` (subagent): C and assembler sources in a Rust service object (`include_dirs`, `-DDK_SERVICE=1`), service-check, Nx inputs, a fixture service, docs. |
| d5ffd1a6e | The host crate `dkrt-svc-instrument`, the `service!` glue, the built object; the hash refresh of `machines.dko.json` (stale since `b359f11bd`, same bytes). |
| d2469e6ab | The SRC-page bug (below); address maps keep C statics; `tests/regression/runtime/instrument-symbols.ts`. |
| dabec5dd2, 8d2fca8ce | The CANOPY needs test counts the service's arena; the PRISM coexistence test reads CANOPY state through the service. |
| 1b52ef0bc | Merge of `feat/canopy-half-tables` (subagent): CANOPY's factory wavetables stored by half and rebuilt on the DSP. |
| 995039018 | `stage_bytes` 131,072 on the DT2. |
| 48c75ca7f, e8dfec64a | The image-declared boot need (`boot_need_<service>`) in the runtime, used by the track keyboard until 48ed2b338. |
| 48ed2b338 | The track keyboard on its own: `core/keyboard-panel`, `core/copy-watch`, the MIDI router in the image, the image tick, `stable`, in the hardware set, eleven proofs registered. |

## Numbers [D]

| | `cf.expansion` used of 1,396,736 | free |
| --- | ---: | ---: |
| installed image `9ee1ed898e39214e` | 1,318,652 | 78,084 |
| `canopy-overloop-prism` at `5936100a1` | 1,340,300 | 56,436 |
| with the move and the 128 KiB stage | 1,193,744 | 202,992 |
| the same with the KEYBOARD view back in the image (`core/keyboard-panel`) | 1,200,904 | 195,832 |
| the hardware set now (with `core/track-keyboard`) | 1,279,340 | 117,396 |
| `core/track-keyboard` with the menu and health, no `core/instrument-play` | 1,241,004 | 155,732 |

- `services/instrument.dks` (dt2): 96,508 bytes (image 74,364, bss 47,828), no hook, three fills; arena need 122,208. It was 104,760
  with an arena need of 128,848 while the KEYBOARD view was in it.
- `canopy.dkm` with the half tables: 102,328 bytes (was 148,280); arena need 188,992 (was 281,152); DSP area 197,312 (was 150,720).
  Rendered PCM equal before and after (subagent: 235 cases; a mutated routine differs).
- A V2 file keeps a copy of its DSP section and the relocated DSP image in the arena while loaded. Before the half tables the
  four files of the hardware set did not fit: PRISM was refused with E_NOMEM.

## Proofs (emulator, DT2 1.17)

On the final layout (48ed2b338), each run, none replayed:

- CANOPY, the seven registered device tests: arp, needs, persistence (save, power cycle, reload), copy-handoff, pattern-copy,
  sound-transfer, track-switch: pass, with the host code in `services/instrument.dks` and the half tables.
- PRISM coexistence (CANOPY, Overloop, PRISM and remote with the machines, fx and instrument services): pass. RELAY's two
  device tests: pass. PRISM send and transition passed on the layout before the track keyboard split (995039018 with the boot need).
- The track keyboard, the eleven registered targets through Nx (panel, midi, sequence, sequence-transition, arp-phase, keys-latency,
  pool, storage, track-clipboard, trig-clipboard on images without `core/instrument-play` and without the service; canopy with both):
  pass. The manual `auto-pool`: three of four runs pass (Open).
- Host: the 25 test files that compile the moved and kept C (283 tests); the runtime crate's 381 host tests (with the boot need
  and the image tick); patchkit's 257 unit tests and its C-sources integration tests (the subagent ran the whole suite: 509 of 510,
  the one failure the stale objects at the base).
- Python: the instrument and CANOPY suites (1,836 at the split); `test_instrument_service_seam.py`, `test_track_keyboard_image.py`,
  CANOPY's `test_canopy_instrument_need.py`, `test_pk_device.py`.
- Static: `patchkit gen --check`, `patchkit build --check` (objects current), the seam `--check`, service-check, ruff on the new
  Python; the layer check has no finding in the new or changed code (its other findings predate this work).
- An A/B run against the baseline tree found the SRC-page bug below; before the fix the arp test failed here and passed there.

## Found on the way

- `instrument_src_tick` compared the vtable draw entry with its own `ip_src_draw`; the vtable holds the image's forwarder. Moved code
  that compares a function address with a firmware table entry needs the image's address (`IP_DATA`). Found by an A/B run against
  the baseline (`ip_src_debug` 8 and 9 were 0 where the baseline had 18).
- The runtime reads `instrument_runtime_policy` on its first tick and in the boot plan's capacity, before services load: it stays image.
- The track keyboard depended on CANOPY in four places (KEYBOARD view, MIDI router, the copy hooks, its start); plan 9.1.
- `machines.dko.json` was stale at the base (hash only). PRISM's `ui.device.test.ts` fails the same way on the baseline.
- Do not run `cargo fmt` in the runtime crate (not rustfmt-clean); an edit of `dkrt-abi` stales every service object.

## Open

- The track keyboard after the upstream merge: estimated a few KB short or none to spare in the hardware set; it becomes its own
  service file (its image boot need loads it), hook sites in the image as for `core/instrument-play`.
- `auto-pool` (track keyboard) stays a manual proof: its panel live-recording step failed once in four runs of this layout and in its
  one run with `core/instrument-play` in the image, both while several emulators ran; it passed both runs on the baseline.
- Not run: the full repository gate, the DN2 halves, CANOPY's manual proofs, the stack measurement of finding 353.
- Hardware: an install needs `services/instrument.dks` on the card beside `machines.dks` and `fx.dks` (`patchkit services` lists it
  for `canopy.dkm`) and the new image. Needs the owner's go.
- Phase B, the upstream merge: upstream `main` is `e5f7cf1e2` (2026-11-24), 961 commits not in the fork's `main` (merge base
  `92ac7fe4a`). Plan section 8 lists what the merge must port; add `core/keyboard-panel`, `core/copy-watch`, the image tick and the
  boot need.
