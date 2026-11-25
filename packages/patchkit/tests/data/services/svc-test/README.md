# Fixture: a service object with C and assembler sources

The test-only service `svc-test` in the layout of a service folder (`services/NAME`: the neutral crate, `tests/`, the service file `dks/`), for
`packages/patchkit/tests/service_c_sources.rs`. The `[[object]]` of `dks/module.toml` lists the crate's `Cargo.toml`, then a C source and two
assembler sources outside `dks/` (`../csrc`), with `include_dirs` for the header they share. `patchkit build` links them into the crate's object:
the hook `cf.menu.hook.settings_list.test` is the assembler routine `svc_test_hook` (a service takes raw hooks only), which calls the C function
`svc_test_settings_tail` and leaves through the tail of the site; the exports `dk_svctest_ping` (assembler) and `dk_svctest_hits` (C) are named by
`dks/src/gen_exports.rs`, and `svc_test_unused` (C) is reached by nothing, so the link drops it.

The service owns no record kind: the crate has no `records.rs`, and its owner `SvcTest` (`src/svc_test.rs`) holds nothing. Services go on the card
packed with a port file; the hook is the test hook of `ports/fw/menu.toml`, which a production port file does not list (core/ui hooks the same site),
so the test checks the packed file against the port files of the test image (`patchkit port check --with-test-hooks`).

It is under no module root (`modules/`, `tests/fixtures/modules/`): its id is `core/services/svc-test`, as the layout of a service folder wants,
and `test/svc-test` provides the same service there. The test names its manifest with `--manifest`, and the object is built into the cache
(`category = "test"`), never into this directory.
