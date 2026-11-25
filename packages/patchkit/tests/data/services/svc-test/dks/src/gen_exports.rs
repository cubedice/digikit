//! The exports of the `svc-test` service file, the `provided` imports of sdk/sdk.toml in its order: what `patchkit gen` writes for a service folder
//! under services/ (this fixture is not there, so the file is written by hand). Both are routines of the C and assembler sources.

/// The entries `service!` declares, by name hash, in the order of `sdk/sdk.toml`.
pub fn exports() -> [(u32, *const u8); 2] {
    [
        (dkrt_svc_rt::name_hash("dk_svctest_ping"), crate::c::dk_svctest_ping as *const u8),
        (dkrt_svc_rt::name_hash("dk_svctest_hits"), crate::c::dk_svctest_hits as *const u8),
    ]
}
