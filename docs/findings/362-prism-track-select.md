# 362. PRISM's tracks chosen with the TRIG keys while G is held

The owner's decision of 2026-11-19: PRISM's per-track PRS send amounts (finding 359) are replaced by tracks chosen on or
off with the TRIG keys, reached from PRISM's INPT knob (encoder G). Digitakt II OS 1.17, on the host and on the emulator
with the SHARC core. Nothing here ran on hardware and nobody has listened. Marks as in `docs/FINDINGS.md`; every result
here is **[D]** (measured or read once in this work) unless it is marked **[O]**.

## The interaction

| Gesture on PRISM's page | What happens |
| --- | --- |
| Hold G | The 16 TRIG LEDs show the tracks PRISM hears: `0x71` (white) for a heard track, `0x02` (dim) for the others. The INPT cell shows the count on its value line (`4 TRK`, `NO TRK`) and `TRIGS` inverted below it |
| TRIG n while G is held | Track n on or off, at the press. No note, no step, no track or pattern change: the key never reaches the firmware's handlers. Its release is the page's too, also after G was released first |
| Release G | Every TRIG key and LED is the firmware's again |
| Short press of G | INPT LIVE / LOOP while there is a loop, as before; without a loop nothing |
| Turn G | Unchanged: INPT while there is a loop |
| G not held | INPT shows `LIVE` or `LOOP`, and under it the number of tracks PRISM hears at LIVE (`2 TRK`, `NO TRK`) |

A release of G is the INPT switch only after a short press: no TRIG key and no turn of G during the hold, and less than
0.4 s (600 blocks of the engine's heartbeat, `PU_SHORT_BLOCKS`). A hold only to look at the LEDs does not switch INPT in
the middle of a transition. Without a fresh heartbeat (no link) any hold counts as short. The switch moved from the press
to the release of G; a turn of G while it is held switches at once, as a turn does, and its release then does not switch
back.

The page takes nothing when it is not on top: a covering view, FUNC + FX to another FX page or another view ends the
selection at once (`pu_leave`), the LEDs and keys go back to the firmware, and the release of G that follows switches
nothing. A TRIG key that was down before G was pressed stays the firmware's gesture.

INPT's state line is drawn 4 pixels left of the cell's centre. In the firmware's label font (about 4.4 pixels a character
measured on the emulator's screen, not the 4 the host harness assumes) `4 TRK` touched H's `NO LOOP`; at the offset the two
are 2 pixels apart. **[O]** `NO LOOP` itself reaches x 124 to 125 at its bottom right in the real font, inside the four
columns the FX view's page indicator owns; this is from before this change (finding 361's page) and was left as it is.

## Where the selection lives, and how it reaches the DSP

- **The value** is `PH_P_TRKS` (bit t for track t + 1) of `core/phrase-control`, one more raw value beside PRISM's others
  (`sdk/dsp/contract/dk_phrase.inc`: `PH_P_TRKS 13`, `PH_PARAM_COUNT 14`, `PH_SEND_FULL 127`). It is saved in PRISM's
  settings record (`phrase/<module id>`, format 3, now 14 values), so it is global like the other values (no kit, pattern
  or project holds it) and survives a restart. It is not a control word: the engine is unchanged (`prism.dkm` has the
  same code).
- **The carry-over.** A record of 13 values (written before the tracks) or none: the values it holds are read, and the
  tracks come from the fx service's old record `fx/send/<module id>` (16 bytes, one a track, finding 64): every track with
  an amount above 0 is heard. With neither record no track is heard. Nothing is written at the boot; the next change of
  any value saves the record with the tracks, and from then on `fx/send/...` is not read. On the owner's device
  (`fx/send/digikit/prism` with 127 for tracks 1 and 11) PRISM hears tracks 1 and 11 after the install.
- **To the DSP.** `core/phrase-control` has a third `[[info]]` function for the fx service, `phrase_sends(amounts)`: 127
  for each heard track, 0 for the others, 1 while there is an owner. The fx service calls it once a UI pass for the phrase
  effect's row and sends the four amount words of the row as live values on the link, as it does for a send cell's
  knob. The send route mixes the bus by them (finding 62). With INPT at LOOP the engine closes the track side of its
  input, as before; the amount words stay as chosen.

## The fx service: no PRS cell, and what it means for other send effects

The smallest change found, in `modules/core/services/fx/src/fxsend.rs` and `phrase.rs`:

- `fxsend::apply`: when the row is the phrase effect's and the image has `phrase_sends`, a send route (`route` 1) is given
  without a cell of the send page; no parameter row is written, `used` (the send slots) is not counted, `fx/send/<id>` is
  not read. Every other effect goes the old way.
- `fxsend::tick`: the early return is `used == 0` and no such row; the loader settle and the Sound arrays run only for
  cells, as before; the phrase row's amounts are taken from `phrase_sends` before the live-value loop.
- `release`: a send route without a cell gives back no cell.
- Without `phrase_sends` in the image the phrase effect is a send effect like any other (it takes a cell).

For other send effects: both of the Digitakt II's cells are theirs; PRISM never takes one and is never left unrun for the
lack of one (finding 359's "if two other digikit send effects are earlier, PRISM loads and is silent" no longer holds).
With PRISM the only send effect the track FX key lists the stock page alone (`fxp_send_count` 0). Their cells, amounts
and settings behave as before: the host test loads a send-only effect after PRISM and it takes cell 1 with its stored
amounts; the device tests of `core/fxpages`, `core/fx` and `core/dynfx` (among them `fx-routes-panel` with
`test/dyn-fx-gain-sendonly` and `fx-track-route`) are in the gate below.

## The hooks

TRIG keys never reach the FX view: the firmware's track and grid handlers take them first. The input overlay that step 2
of the redesign removed had the two hooks that reach them, and they ran on the owner's hardware; they are restored as
they were (the windows, hashes, addresses and modes of git `98409b9ad`), now owned by `core/phrase-ui` directly (not by
`core/fxview`, whose table slots are untouched):

| Hook | Site | Handler |
| --- | --- | --- |
| `cf.page.hook.phrase_key_dispatch`, before | `cf.page.site.phrase_key_dispatch`, the primary ViewController key dispatcher (`0x4011c79c`) | `phrase_key_dispatch`: a TRIG event goes to the model while G holds the selection on the page on top, or when it is the release of a TRIG key the model took; everything else goes on unchanged |
| `cf.page.hook.phrase_led_set`, before | `cf.page.site.phrase_led_set`, the LedManager setter (`0x400308e0`) | `phrase_led_set`: while G holds the selection, logical LEDs 26 to 41 get the page's value at layer 2; the next LED frame clears every claim |

`devices/fw/phrase-ui.toml` has the two sites and the two hooks again; the DT2 1.17 port file the four entries, by hand
(two anchors, two hooks), since `mise run patch:port` stops at an unrelated anchor in this checkout (finding 359; not run
here). The image applies, passes preflight and verify, and boots in every device test below. No hook into audio code was
added.

## Proofs

Lowest layer first, on the branch head in the WSL checkout; device tests on the Digitakt II 1.17 image on the emulator
with the SHARC core.

| Layer | Check | Result |
| --- | --- | --- |
| Host, C | `core/phrase-ui:test-host`: G held and the TRIG keys, their LEDs (stock without G, dim and white with it, other LEDs untouched), INPT's texts held and not held up to `16 TRK`, a TRIG key released after G, one pressed before G, the page losing the top while G is held, and which holds switch INPT with a loop (short, with a TRIG key, with a turn, 600 blocks, 599 blocks, no heartbeat) | pass |
| Host, C | `core/phrase-control:test-host`: the owner's case (13 values and `fx/send/...` with 127 at tracks 1 and 11: tracks 1 and 11, the values kept, nothing written), a change saves 14 values, a restart reads them and not the send amounts, no track saved as such, any amount above 0 heard, nothing stored is no track, a send record of another size not read, the clamp, the tracks change no control word, `phrase_sends` with and without an owner | pass |
| Host, Rust | `dkrt-svc-fx:rust-tests`, 46 cases. New: the phrase row on a send bus without a cell, its amounts from `phrase_sends` before and after a change (live values of the right words), no `fx/send` write; a send-only effect after it takes cell 1 with its stored amounts; without `phrase_sends` a cell as before; the three names hashed as the adapters declare them | pass |
| Assembly | `core/runtime:test-assembly`, new case: the info tables of the sets `phrase` and `canopy-overloop-prism` have `phrase_bind`, `phrase_sends` and `phrase_ui_tick` under the hashes the fx service looks up; `emulator` has none | pass |
| Device | `ui`: no track stored, `NO TRK`; G held: every TRIG LED read from the panel `0x02`, INPT `NO TRK` and `TRIGS`; TRIG 3: track 3's amount in PRISM's DSP row `127 << 16`, its LED `0x71`, the others `0x02`, `1 TRK`; TRIG 3 again: 0 and dim; tracks 1, 5, 9 and 16: the row `[1, 127, 127, 127, 127 << 24]`; G released: the 16 LEDs equal the ones before the hold, `LIVE` and `4 TRK`, TRIG 2 goes to the firmware; a short press of G without a loop changes nothing | pass |
| Device | `send`: no send page (`fxp_send_count` 0, no cell used) and the stock page on the second FX press; G held and TRIG 1 with the test instrument on track 1: the capture peaks below 1e-4 (no note), the key dispatcher took the press and the release, the row `[1, 127, 0, 0, 0]`; GRID RECORD on (the RECORD LED changes), G held and TRIG 2 twice, RECORD off, a cycle of PLAY peaks below 1e-4 (no step); track 2 not chosen: its sound equal sample for sample to the one with no track; track 1 chosen: the echo starts **2880** frames after the note (the engine's 2880), gain **0.99997** against the send law's 1 times MIX 1, residual 0.77 %, nothing before it; the card without PRISM: the dry path equal sample for sample; **the owner's card** (`fx/send/digikit/prism` 127 at tracks 1 and 11, no PRISM record): the row `[1, 127, 0, 127 << 16, 0]`, PRISM hears tracks 1 and 11, `2 TRK` | pass |
| Device | `transition`: track 1 chosen with G and TRIG 1; off and on again while PRISM hears the loop, INPT stays LOOP and the output repeats with the loop's cycle (1.5e-7 and 1.9e-7 of the peak); a short press of G switches INPT to LIVE; the rest as in finding 361 | pass |
| Device | `coexistence`: CANOPY's track 1 chosen with G and TRIG 1, PRISM's row `[1, 127]`, `stallMs` 1500 | pass |
| Rig | `digikit/prism:test-rig`, `digikit/overloop:test-rig`, `@digi/sharc-rig-kit:test-conformance-rig` (189 cases); `test-conformance` of PRISM and Overloop | pass |
| Static | lint, fmt-check, rust-fmt, typecheck, python-checks; `check:layers`; `patchkit:objects-check`; `patchkit gen --check`; device-literals | pass |
| Gate | `mise run test:subset -- A:core/phrase-ui A:core/dynfx A:core/fxpages A:core/fx A:core/instrument-play A:core/loop-display --run`: 36 device test halves (PRISM's four, Overloop's transition, the 13 files of `core/fxpages`, `core/dynfx` and `core/fx` on DT2 and DN2, among them `fx-routes-panel` with the send-only fixture and `fx-track-route`, CANOPY's seven on DT2), `gate-smoke`, `gate-pins` | every task passed but PRISM's `send`, which failed on an assertion new in it (the RECORD LED read 0 before the panel had sent it a value); with that fixed it passed alone at the head. It skips every device test without these tags: the runtime, storage, loading, transport, LFO, sample and tool tests |
| Python | `py_select run --base 65f7aa4a0` | two files fail, both the same at the base commit: `test_pk_new.py` (`[lfo]` and `test_a_service_template_is_the_crate_layout_with_every_token_replaced`) and `test_pk_current.py` (`test_ports_are_current`, stops at `cf.page.fn.source_turn`); every other selected file passes |
| Hardware set | `patchkit apply --set canopy-overloop-prism`, `preflight`, `verify`, `pack-all` of canopy, overloop, remote and prism, `services` with `--bind-dir` | every step exit 0; nothing installed. `cf.expansion` 1,317,884 of 1,396,736 bytes, `dsp.l2` 566,380 of 922,756, image 2,143,264 bytes, **image id `d0e4825dd04d8646`**; `prism.dkm` 9,994 bytes, `fx.dks` 48,436; the packages, the services and the image are the ones the device tests loaded |

Not run: `mise run test:full`, `mise run patch:port`, anything on hardware.

## What the owner's device does after the install

PRISM's values are kept (the record is read with its 13 values); PRISM hears tracks 1 and 11, as their PRS amounts of 127
gave; the second page of the track's FX key is gone (no other digikit send effect is loaded in the fork's set). Holding
G shows tracks 1 and 11 lit.

## Open

- **[O]** Hardware: the two hooks ran on the owner's Digitakt in the old overlay; this use of them has not. Whether the
  LED values `0x71` and `0x02` look as intended on the TRIG keys is for the owner to see.
- **[O]** The 0.4 s limit of a short press is a first value.
- **[O]** `NO LOOP` in the H cell reaches the page indicator's columns in the real font (above).
