//! The routines of the C and assembler sources of the object (`[[object]] sources` of module.toml): the raw hook and the two exports. A host build (the crate's
//! `rust-tests`, which compiles the `service!` call) has stand-ins instead, so that their addresses exist.

#[cfg(target_arch = "m68k")]
extern "C" {
    /// `../csrc/hook.s`: the raw hook of `cf.menu.hook.settings_list.test`. It saves d0, d1, a0 and a1, calls the C handler `svc_test_settings_tail` of
    /// `../csrc/hook.c` with a2 (the settings menu), which counts the run, and leaves through the tail of the site.
    pub fn svc_test_hook();
    /// `../csrc/hook.c`: how often the hook ran.
    pub fn dk_svctest_hits() -> u32;
    /// `../csrc/exports.s`: 42.
    pub fn dk_svctest_ping() -> i32;
}

#[cfg(not(target_arch = "m68k"))]
pub extern "C" fn svc_test_hook() {}
#[cfg(not(target_arch = "m68k"))]
pub extern "C" fn dk_svctest_hits() -> u32 {
    0
}
#[cfg(not(target_arch = "m68k"))]
pub extern "C" fn dk_svctest_ping() -> i32 {
    42
}
