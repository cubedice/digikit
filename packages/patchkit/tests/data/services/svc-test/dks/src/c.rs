//! The routines of the C and assembler sources of the object (`[[object]] sources` of module.toml): the hook handler and the two exports.

extern "C" {
    /// `../csrc/hook.c`: the handler of `cf.menu.hook.settings_list.tail` (`menu`: a2 at the site); it counts its runs.
    pub fn svc_test_settings_tail(menu: *mut u8);
    /// `../csrc/hook.c`: how often the hook ran.
    pub fn dk_svctest_hits() -> u32;
    /// `../csrc/exports.s`: 42.
    pub fn dk_svctest_ping() -> i32;
}

/// The first step of `init`: nothing to bind.
pub unsafe fn setup(_core: &dkrt_svc_rt::ImportCore) -> i32 {
    0
}
