# 359. PRISM as a send effect, and its one page

Step 2 of the redesign in [the plan](../plans/prism-overloop-redesign.md): PRISM is an ordinary send effect of the effect
framework with one native page, and `core/phrase-route` is gone. Digitakt II OS 1.17, on the emulator with the SHARC core
and on the host. Nothing ran on hardware and nobody has listened. Marks as in `docs/FINDINGS.md`; every result here is
**[D]** (measured or read once in this work) unless it is marked **[O]**.

## What changed

| | Before | Now |
| --- | --- | --- |
| Route | `core/phrase-route`: four hooks in the stock send and master code, private copies of 18 buses, an 18-bit source mask, capture gains, EXT L/R, ducking of the selected tracks | The effect framework's send route: input = the tracks times their PRS send amounts, output added to the FX return, dry untouched |
| Hooks into stock code | 4 DSP hooks (router), 2 ColdFire hooks (TRIG LEDs, key dispatcher), 5 table slots (page) | 5 table slots (page). No hook of PRISM's own is left; the frame suffix and the reply carrier of `core/phrase-control` are as before |
| Page | five pages and a TRIG overlay | one page (below) |
| Control words | version 2: the tape delay, the grains and the capture | version 3: 9 words for the repeater, 3 kept for the loop |
| Image of the fork (`canopy-overloop-prism`) | `cf.expansion` 1,327,524 of 1,396,736 bytes; `dsp.l2` 574,276 of 922,756 | `cf.expansion` 1,314,992 (12,532 fewer, 81,744 free); `dsp.l2` 565,804 (8,472 fewer) |
| `prism.dkm` | 12,330 bytes | 8,938 bytes |

Removed: the module `core/phrase-route` with its guard copy, `devices/fw/phrase-route.toml` (five anchors, four hooks), the
LED and key-dispatch anchors and hooks of `core/phrase-ui`, their 13 entries in `devices/ports/dt2-1.17.toml`, the
`_phrase_route_info` import of `core/dyndsp`, the subsystem name `phrase`, the Python rig's phrase provider (only its own
tests used it) and the router half of the probe tool (now `tools/patchkit/pk_phrase_transport_probe.py`). Patch sets
`phrase` and `canopy-overloop-prism`, the image roots `prism` and `prism-coexist` and PRISM's `requires` no longer name it.
`core/phrase-control` now requires `core/loop-storage` itself (the router used to bring it; the engine imports its lease).

## The send path

- PRISM's record declares the send route only (`dk_fx_routes(fx, DK_ROUTE_SEND, DK_ROUTE_SEND)`: flags send, no master,
  default send). The fx service gives it the first free cell of the send page when its page is registered; its DSP row
  then has route 1 and the four amount words, and the production send code (`core/fx`, finding 62) mixes the bus and adds
  the output to the return.
- The fx service no longer treats the phrase effect's route specially. What stays special: one phrase effect at a time,
  its row MIX is unity (`fx_set_mix`), and the kit's page values are not read for it.
- **[C]** (finding 65, "an effect past the limit stays on the master, also a send-only one") A send-only effect without a
  cell used to be put on the master chain, which its record forbids; there PRISM would replace the whole mix with its wet
  signal. Now such an effect is run by neither route (route word 2) from its claim on, until `fxsend::apply` gives it a
  cell (`claim_route`, `route_word`; host tests in `fxsend.rs`). This holds for every send-only effect: the fixture
  `test/dyn-fx-gain-sendonly` is the other one in the tree, and no test had one past the limit. An effect that allows the
  master still stays on the master past the limit, as before.

**Which slot, and what it displaces.** The Digitakt II has two digikit send cells (`send_slots = 2`), on the second page
of the track's FX key; the stock delay, reverb and chorus sends are on the first page and are not touched. PRISM takes the
first free cell in load order. In the fork's set (CANOPY, Overloop, PRISM, remote) Overloop is a master effect, so PRISM
has cell 1 (encoder A, label `PRS`). The owner does nothing to put it there. If two other digikit send effects are
earlier in the selection, PRISM loads and is silent; DIGIKIT > FX ROUTES offers no other route for it.

**One MIX.** The level is the engine's `PH_C_MIX`, set by LEVEL on PRISM's page and kept with PRISM's other values in the
settings. The framework's row MIX stays at unity. Reason: the page is native and has no framework knob, the framework's
MIX is a kit value and PRISM's values are global, and the engine's MIX is already smoothed and proven on the rig.

**One home for the layout.** `sdk/dsp/contract/dk_phrase.inc` (version 3). The provider and the page include it through
`<dk/phrase.h>`, the engine through `dk_phrase_imports.inc`; `modules/fx/prism/src/prism.h` keeps only the engine's own
constants and state. The device tests read the same file. The words `PH_C_INPUT`, `PH_C_XFADE`, `PH_C_ARM_SEQ` and
`PH_S_INPUT`, `PH_S_LOOP`, `PH_S_ARMED` have their places and are zero, and the raw values `PH_P_INPT` and `PH_P_XFAD`
exist and refuse a set, so that step 4 changes no layout.

## The page

**Mechanism.** Nothing new was needed in the firmware. `core/phrase-ui` writes five table slots (the FX view's draw, key,
encoder and encoder-interface entries, and the help view's draw entry). Each callback calls the stock function unless the
page the view shows is the owner's page code (`dkrt_phrase_active`) and the view is the visible FX view. That test never
depended on the route; the tie to the route was in the fx service (it forced route 2 and required the router's export).
So a send effect owns its page, eight encoders, LEVEL and the encoder presses, and every other FX page and the track FX
pages stay stock. Seen in the device tests with `core/phrase-ui` in the image: FUNC + FX leaves PRISM's page for the next
FX page, SETTINGS covers it and LEVEL there does not move MIX (`ui`); the knobs of Overloop's FX page set Overloop's
values (`coexistence`); the PRS knob on the track's FX page sets the send amount (`send`).

The real page (emulator, 128 x 64, three times enlarged in `firmware/hardware/prism-page-shots/` of the main checkout):

```
 PRISM                              ECHO    387 MS      header: title, zone, the time the engine plays
─────────────────────────────────────────────────
 MIX   ZONE       RATE       RPTS       COLR
 ┌─┐   ECHO       387 MS     48         0             ZONE: the zone's name, eight marks under it
 │ │   ····▮···
 │█│   [FLIP]     FREE       [HOLD]     DARK          state line: FLIP and HOLD inverted when on
 │█│  ───────────────────────────────────────────
 │█│   HALO       SKEW       INPT       XFAD
 └─┘   0          0          LIVE       --
 100                                    NO LOOP
```

Where it differs from the plan's drawings:

- No pattern name (`A02`) at the left of the header: the page clears the whole header, as the page before it did.
- Text only; no native knob graphics. A cell is 26 pixels wide, which is six or seven characters of the label font; a
  time of six characters fills its cell, so the zone's name and the time beside it stand two pixels apart.
- The eight marks of ZONE are under the zone's name, not above it.
- COLR shows the distance from the centre on the value line and `DARK` or `BRIT` on the state line (`DARK 20` does not
  fit a cell). The centre shows `0`.
- `FLIP` is on the state line of the ZONE cell, not beside the zone's name.
- RPTS shows the number 0 to 127; `INF` only with HOLD. 96 is unity (a repeat holds its level).
- SKEW shows `L n` or `R n`: the side with the longer time, and `0` at the centre.
- Times: `387 MS` below a second, `1.25 S` below ten, `10.7 S` from there. The header's time is the engine's and glides
  after a free RATE move; with SKEW it is the geometric mean of the two sides.
- RATE free has 1016 fine steps across a zone; a tick is 8 of them, FUNC + tick is one. With SYNC there are sixteen note
  lengths, 1/32 to two bars, four ticks each.

**An encoder's turn right after its own press is not delivered.** In the device test the first turn of encoder B within
about 0.3 s of a press of B never reached the FX view's turn entry (neither of two turns at about 120 and 270 ms after the
release; every turn from about 320 ms on; a turn of another encoder at once was delivered). The page's code is not on that path. **[O]** whether
this is the stock firmware's decoder or the emulator's panel input, and whether the hardware does it; it matters for
"press B for SYNC, then turn B".

## An old kit

- A kit saved while PRISM was the old effect holds, for PRISM, only the page values of an effect row (its MIX; PRISM never
  had knobs). The fx service keeps the phrase row's MIX at unity whatever it is given
  (`the_phrase_row_takes_its_route_and_keeps_its_mix_at_unity`, `modules/core/services/fx/tests/records.rs`), and its poll
  of the kit's values skips the phrase row (`fxpage.rs`, read, not tested here). The kit loads as any kit does and changes
  nothing in PRISM.
- A stored master route for PRISM is not used, because the record no longer allows it
  (`the_saved_route_falls_back_to_the_default`, `fxsend.rs`).
- PRISM's own values were and are global settings, not kit data. The old record (format 2: five pages of values, the
  source mask, the gains) is not read; PRISM starts at its defaults (`control_harness.c`, "a record of another format").
- What the owner sees after installing: PRISM is silent until a PRS send amount is raised on a track, because there is no
  source mask any more and every amount starts at 0. The old PRISM settings are gone. Nothing else in a project changes.

## Proofs

Lowest layer first, all on the branch head in the WSL checkout; "pass" is the command's exit status 0. Device tests are
the Digitakt II 1.17 image on the emulator with the SHARC core (`dsp: 'sharc'`).

| Layer | Check | Result |
| --- | --- | --- |
| Host, C | `core/phrase-control:test-host`: the values to the control words and their ranges; the note table (sixteen lengths, 3,000 to 192,000 frames at 120 BPM; other tempos; no tempo keeps the last time); the checksum; persistence (HOLD not kept, a record of another format or size ignored, values clamped); the status assembly; the receiver and the reply on the SHARC rig (12 cases) | pass |
| Host, C | `core/phrase-ui:test-host`: each encoder to its value and step, the three presses, the keys that go to the stock view, FUNC + RATE, the note steps with SYNC, every text, the cells of the plan's two drawings, and everything drawn inside the screen | pass |
| Host, Rust | `dkrt-svc-fx:rust-tests`, 43 cases. New: the route word of a send-only effect with and without a cell and at its claim; a stored master route of a send-only effect; the phrase row takes its route and keeps MIX at unity | pass |
| Host, Python | `tests/patchkit/test_fx_rig_allocation.py` (the Python rig without its phrase provider), 7 cases | pass |
| Assembly | `core/runtime:test-assembly` | pass. No case added: no service or runtime mechanism is new (the fx service finds one function fewer by name) |
| Rig | `digikit/prism:test-rig`, 47 cases; the heaviest block is 17,381 instructions, the `dsp_cost_fx` of `module.toml` (two more than in finding 358: the status publication clears every word the layout does not name) | pass |
| Rig | `@digi/sharc-rig-kit:test-conformance-rig`, 189 cases | pass |
| Device | `digikit/prism:test-device--ui`: the page opens; LEVEL and the encoders A to F each move their value, their control word in the DSP and their cell; B, A and C switch SYNC, FLIP and HOLD (the note length reaches the engine as 18,000 frames at 120 BPM and the engine reports it; HOLD is reported as held); FUNC + RATE moves one fine step a tick; G, H, the presses of D to H and FUNC + NO change no value and no control word; a covering view and FUNC + FX take the controls away | pass |
| Device | `digikit/prism:test-device--send`: track 1 with PRS amount 64, zone SLAP at 60 ms, RPTS 0, MIX 127. The added signal starts **2880** frames after the note (the engine's time: 2880) and is nothing before; its gain against the dry note is **0.253946** (send law 64² / 127² times MIX: 0.253953), residual 0.8 % of the echo. A note of track 2 (amount 0) with track 1's amount at 64 equals the note with every amount at 0, sample for sample. Track 1 with every amount at 0 equals the same card without PRISM in the selection, sample for sample. No PCM pin was set: the checks are these numbers | pass |
| Device | `digikit/prism:test-device--coexistence`: CANOPY, Overloop, PRISM and remote on one card; Overloop loops CANOPY, PRISM repeats CANOPY's send and holds it; both effect rows ran with no guard fault, the banks are distinct, `stallMs` 1500 as before (finding 350) | pass |
| Device | `digikit/prism:test-conformance` (the load cell on the patch set `phrase`: PRISM is uploaded through USB, loads and registers without a runtime fault) | pass |
| Hardware set | `patchkit apply --set canopy-overloop-prism`, `preflight`, `verify`, `pack-all` of canopy, overloop, remote and prism, `services` with `--bind-dir` (fx.dks, machines.dks) | every step exit 0; nothing installed. Image 2,141,984 bytes |
| Static | lint, fmt-check, rust-fmt, typecheck, python-checks; `check:layers`; `patchkit:objects-check`; `patchkit gen --check`; device-literals | pass |
| Gate | `mise run test:subset -- A:core/phrase-ui A:core/dynfx A:core/fxpages A:core/fx A:core/instrument-play --run`: the 13 device test files of the FX adapters and the fx service on DT2 and DN2 (24 halves), PRISM's three, CANOPY's seven on DT2, `gate-smoke`, `gate-pins` | pass (35 minutes). It skips every device test without these tags: the runtime, storage, loading, transport, LFO, sample and tool tests |
| Python | `py_select run --base` (the tests the change selects: all of `tests/patchkit` and the users of the Python FX rig) | two files fail, both the same way at the head of step 1: `test_pk_new.py` (`test_a_template_is_copied_with_every_token_replaced[lfo]`) and `test_pk_current.py` (`test_ports_are_current`: the resolver stops at `cf.page.fn.source_turn` in this checkout, see Open). Every other file passes, among them `test_fx.py`, `test_overloop.py`, `test_fx_storage_budget.py`, `test_loop_display.py` |

Not run: `mise run test:full`; anything on hardware; `mise run patch:port` (see Open).

## Open

- **[O]** Hardware: nothing here ran on a Digitakt, and nobody has listened.
- **[O]** The dead time after an encoder's own press (above).
- **[O]** The port file was edited by hand (13 entries removed). `mise run patch:port` and `test_ports_are_current` could
  not confirm it in this checkout: the function database here differs from the one the port was made with, and the
  resolver stops at an unrelated anchor (`cf.page.fn.source_turn`), at the head of step 1 as well. The image builds,
  passes preflight and verify, and boots in the device tests. Someone with the port's database should run
  `mise run patch:port` once.
- The seam between the fx service and the phrase adapters (it finds `phrase_bind` and `phrase_ui_tick` by name in the
  image's info table) has no assembly case; it had none before, and only the device tests cover it.
- `pp_phrase_timing` in `core/pattern-position` has no caller left (the old provider's musical clock used it). It is left
  in place: the module is Overloop's, which step 3 reworks.
- Steps 3 and 4: INPT, XFAD, ARM, FUNC + NO, and the loop as PRISM's input.
