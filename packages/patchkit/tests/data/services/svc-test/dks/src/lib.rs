//! The `svc-test` service as a service file: the `service!` call and nothing else. The hook and the exports are routines of the C and assembler
//! sources of the object (`../csrc`); `c.rs` declares them.
#![no_std]

use dkrt_svc_svc_test::records::SvcTest;

mod c;
#[rustfmt::skip]
mod gen_exports;

dkrt_svc_rt::service!(
    SvcTest,
    "svc-test",
    // the service owns no record kind
    &[],
    setup = c::setup,
    hooks = [("cf.menu.hook.settings_list.tail", c::svc_test_settings_tail)],
    fills = [],
    exports = gen_exports::exports()
);
