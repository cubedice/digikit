# 65. Manifest routes and SETTINGS > FX ROUTING (FX routing step 4, Digitakt II 1.17, 2026-10-25)

A run-time effect is a master insert or a send effect (finding 62), and the amounts of a send are the framework's (finding 64). Step 4 gives the effect's author a way to say where the effect starts and which routes make sense, and gives the user a page to choose
(SETTINGS > FX ROUTING, from E1 on; the DIGIKIT menu has no route panel). Marks: **[D]** measured or read once by the author (no second agent has checked it), **[O]** open. Pieces: `packages/patchkit` (`typed.rs` `FxDef`, `register.rs` `fx_routes`, `dkmdsp.rs` effect flags), `modules/core/runtime` (`dsp.rs` `fx_flags`,
`fxsend.rs` `routes_of`, `saved_route`; `fxpage.rs` `rebuild_list`; export `dkrt_settings_flush`), `services/fx` (`src/routing.rs` and `dks/src/ui.rs`: the FX ROUTING row and its popups; `fxsend` `describe`), the fixtures `tests/fixtures/modules/dyn-fx-gain-send` and `dyn-fx-gain-sendonly`, the scenario
`REF:tests/digikit/scenarios/fx-routes-dt2.toml` (`tools/patchkit/pk_digikit_fxroutes.py`). The routes as `services/fx` runs them now (the fork's send-only and phrase changes ported onto the service's owner, 2026-11-24): `src/dsp/mod.rs` (`claim_route`, `fx_set_mix`),
`src/fxsend/route.rs` (`routes_of`, `saved_route`, `route_word`), `src/fxsend/apply.rs`, `tick.rs`, `effect.rs` (the cells, the phrase effect's amounts, the routes offered), `src/phrase.rs`.

## The manifest rule **[D]**

`[[fx]] route` is the default route, `"master"` (when left out) or `"send"`. `[[fx]] routes` lists the routes the user may choose. Left out it is `["master"]`; with `route = "send"` it is `["send", "master"]`, so a send effect can always fall back to the master (the master has no slot limit)
and an effect that says nothing is an insert that cannot move. `routes = ["send"]` makes a send-only effect. `patchkit pack` refuses: a name that is not `master` or `send`; an empty `routes`; a route listed twice; a `route` that `routes` does not list; any send route on an effect without a `tab`
(the send knob and the route row are labelled with the page's tab, so an effect without a page is an insert). Unit tests: `register::tests::the_routes_of_an_effect_follow_the_manifest_rule_and_bad_ones_are_refused`, `dkmdsp::tests::the_routes_are_the_flags_of_the_record_and_zero_is_master_only`.

## The record **[D]**

The effect record's flags word (offset 16, always 0 before) carries both: bit 0 `FXF_SEND` (send allowed), bit 1 `FXF_NOMASTER` (master not allowed), bit 2 `FXF_DEFAULT_SEND`. The word 0 that every earlier record has means master only, default master, so an old `.dkm` loads as it did and a module that
leaves the fields out packs to the same bytes as before (the unit test pins word 0 for master only). Values: 0 is the legacy master-only value; 1 both, default master; 5 both, default send; 7 send only. The packer writes the placeholder 1 (MASTER|SEND, `FXF_SEND`); the runtime overrides it at commit with the declared routes (master only, 0, when none are declared). The packer's parser and the runtime's loader refuse other combinations (`E_DSP_FORMAT`): an unknown bit, a default
that is not allowed. No contract bump: the contract is still fx 1.0, because the layout only gives meaning to a word that was 0 (a runtime older than the flags ignores them and runs the effect on the master; **[O]** a send-only effect on such a runtime is an insert). The runtime keeps the flags per effect row (`DKRT_DSP.fx_flags`) and the allowed routes and the default per row in `DKRT_FXS` (`allow`, `dflt`).

## What the runtime does with it **[D]**

`fxsend::apply` (when the effect row is registered, at the boot): the route is the stored `fx/route/<module id>` when it is 0 or 1 and the effect allows it, else the manifest's default. An effect with no setting and `route = "send"` starts as a send effect. A send route needs a free send cell
(`send_slots`, CAPACITY `SEND_SLOTS`); an effect past the limit stays on the master when its record allows the master (FX ROUTING does not offer a send past the limit, but a default can exceed it). A send-only effect without a cell is run by neither route (below). On an image without send cells (the Digitone II until step 6) every effect that may run on the master stays there and the page lists
it as saved. The fx service's Describe part gives, for the MODULES reply, one word per effect row of a module that has a page, in row order: the row, the route in force, the route the settings give now, the allowed routes and the default (the old `dkrt_fx` export answers `E_NOT_FOUND`). `dkrt_settings_flush()` writes the settings record at once
(a restart needs the write to land first; the 2 s delay would lose it).

**The route word of the DSP row.** 0 master, 1 send bus, 2 neither (the chain skips the row; [finding 359](359-prism-send-effect-and-page.md)). A send-only effect (`FXF_NOMASTER`) has word 2 from its claim on (`dsp::claim_route`) until `fxsend::apply`
gives it a cell (`route_word(allow, send)`): on the master its wet signal would replace the mix. A send-only effect past the send slots, and every send-only effect on an image without send cells, stays at 2: it loads and is silent, and FX ROUTING offers no other
route for it. A stored master route for an effect that is send-only now is not used (`saved_route` gives the default, send). Host tests: `fxsend::tests::an_effect_without_a_cell_runs_on_the_master_only_if_it_may`, `a_send_only_effect_without_a_cell_runs_on_neither_route`, `the_saved_route_falls_back_to_the_default`.

**The phrase effect (PRISM) has a send bus and no cell** ([finding 362](362-prism-track-select.md)). The phrase effect is the one effect of a module whose DSP section imports `_phrase_control_info` (`src/phrase.rs`; one at a time, checked and admitted by the
service's `DspPart`). PRISM's record allows the send route only (default send). In an image with `phrase_sends` (an `[[info]]` function of `core/phrase-control`, found at the file's start) `fxsend::apply` gives its row the send route (word 1) without a cell of the send page:
no parameter row, `fx/send/<module id>` neither read nor written, the send slots not counted (CAPACITY `SEND_SLOTS` `used`, and the slots against which FX ROUTING judges another effect's send). Its amounts are what `phrase_sends` gives (127 for each track its page selects),
read at the apply (none before the adapters have bound the effect: zero, and the next pass has them) and every UI pass after it, and sent as live values of the row's four amount words like a cell's; the DSP's copy follows the link's confirmation. FX ROUTING keeps SEND open for it
while the image has a send page. Both cells of the Digitakt II stay for other send effects: one that loads after PRISM takes cell 1 with its stored amounts. Without `phrase_sends` the phrase effect is a send effect like any other and takes a cell. Its row's MIX stays at unity
(`dsp::fx_set_mix`), whatever its record or a kit says. Host tests: `fxsend::tests::the_phrase_effect_takes_its_amounts_from_its_adapters_and_no_cell`, `the_phrase_effect_takes_no_send_slot_from_the_routes_offered`,
`tests/records.rs` `the_phrase_row_takes_its_route_and_keeps_its_mix_at_unity`, `one_phrase_effect_is_admitted_and_its_release_revokes_the_page`.

**The order of the FX view.** `fxpage::rebuild_list` builds the list of FUNC + FX as DEL, REV, CHO, the send effects, the master effects, each in row order. `fxsend::apply` calls it again once the route of the row is known (`fxpage::relist`), so the list is right when the main view is built; no new hook.

## The page

The route is chosen in SETTINGS > FX ROUTING (E1: the rows of an owner are keyed by its record name through `dkrt_record_name`, and a pick writes `fx/route/<module id>`, 1 send, 0 master, and reloads the effect's module, so no restart and no confirm is needed; a toast shows the result: `NAME: ROUTE`, `NAME: ROUTE ON STOP` when the transport plays (the runtime reloads the module at stop with `dkc_reload_when_possible`), `NAME: ROUTE PENDING` for a pick that waits for its reload, `NAME: RESTART TO APPLY`, `NAME: NOT SAVED`, `NAME: NO ROUTE FREE`, `BUSY: TRY AGAIN` or `NO EFFECTS LOADED`; a toast cuts a long name to 13 characters in the middle, the popup cuts a name over 19 characters the same way). The DIGIKIT menu has no route panel, no ROUTE line and no route write, and none of the statics `DK_FXR*`, `DK_ROUTE_N` and `DK_ROUTE_ITEM`; their host directory ids (18 to 20, 22, 23) are reserved and resolve to nothing. The rules below are unchanged and live in the runtime: a master-only effect offers no send; a send-only effect offers no master; a send is not offered when the slots are full (the phrase effect's send takes no slot, and SEND stays open to it while the image has a send page); the offer is judged against the saved route, not the route in force; two effects of one module share the module's setting (the key is per module id); an effect without a page is not listed.

## Proof of the runtime half **[D]** (the menu half moved to SETTINGS > FX ROUTING in E1)

All 47 checks pass (135 s, 29 emulators, 8 parts of one boot each; the write-watch and flush checks over every boot are clean, the guest wrote only the selection and settings sectors in the store range). Fixtures: `test/dyn-fx-gain` (no route fields), `test/dyn-fx-gain-send`
(`route = "send"`), `test/dyn-fx-gain-sendonly` (`routes = ["send"]`), and the second again under another module id.

- **Old records load as master only.** `test/dyn-fx-gain` has no route fields: its record has flags 0 (unit test), the runtime holds allow = master, default = master (`DKRT_FXS`), the row's route word is 0, and the route row says `GAN  MASTER`. The stock effect of every earlier scenario
  (`fx-route-dt2`, `fx-dyn-dt2`, `fx-persist-dt2`, `fx-pages-dt2`) is this module.
- **A module with `route = "send"` and no stored key starts as a send effect.** `test/dyn-fx-gain-send` with no setting: allow = master | send, default = send, row route word 1, the FX key's count cell 2 (page 2 exists), CAPACITY `SEND_SLOTS` 1 of 2 used (parts `list`, `apply`).
- **A stored route that the effect does not allow falls back to the default.** A stored `send` for the master-only effect and a stored `master` for the send-only one: route words 0 and 1, and the page shows `GAN  MASTER`, `GSO  SEND` (part `fallback`).
- **The FX view's order.** With a master effect in row 0 and a send effect in row 1 the list the runtime hands the firmware is the three stock pages, then row 1, then row 0, and FUNC + FX shows `Test Gain Send (4/5)` and `Test Gain (5/5)` (`step4-fxview-4.png`, `-5.png`).

## Open **[O]**

- **[C]** The Digitone II has one send cell since step 6 (finding 67): `fx-routes-dn2` runs the whole scenario with the limit 1 (the `full` boot has two effects and `NO SEND SLOT` on the second). The FX view's order needs no hook on either device (`fxpage::relist`).
- A send-only effect past the send slots runs on neither route (above): it loads and is silent. A module with two effects shares one route setting.
- The MODULES word of an effect that runs on neither route (route word 2) gives master as the route in force: the word has no value for none yet, and the hosts read the nibble as 1 master, 2 send.
- The send-only and phrase routes on the merged tree (`services/fx` owner, 2026-11-24) are proved by host tests only: no device test ran on it (the runtime's port, the DSP link window of the V2 data and the device tests of PRISM are later items of the merge).
- A power cycle within 2 s of a settings change loses the change (the write waits 2 s). A restart through `dkrt_restart` (MODULES' APPLY & RESTART, the mailbox) does not: it writes a pending settings record first (`settings::flush_if_dirty`; scenario `settings-dt2` part `restart`: a put and at once a restart keep the value).
- A second agent checks the record flags and the list order against the image bytes before the marks become **[V]**.
