//! The `svc-test` service as a service file: the `service!` call and nothing else. The hook and the exports are routines of the C and assembler
//! sources of the object (`../csrc`); `c.rs` declares them.
// a host build (the crate's `rust-tests`) has std; the file is `no_std` on the ColdFire
#![cfg_attr(target_arch = "m68k", no_std)]

use dkrt_svc_svc_test::svc_test::SvcTest;

mod c;
#[rustfmt::skip]
mod gen_exports;

dkrt_svc_rt::service!(
    SvcTest,
    "svc-test",
    // the service owns no record kind
    &[],
    parts = [],
    hooks = [("cf.menu.hook.settings_list.test", c::svc_test_hook)],
    fills = [],
    exports = gen_exports::exports()
);
