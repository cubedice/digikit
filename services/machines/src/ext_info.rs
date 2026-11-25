//! The image table `machines_ext_info` of `core/machines` (`mf_ext_info`, an `[[info]]` of its module.toml): the adapter functions that
//! the service file's hooks call for the SRC pages of an added type (`src-pages.c`: `mf_srcpage`) and for the modulation routes of a persistent instrument
//! (`instrument_mod_param` of core/instrument-play: `mf_typearg`). The owner copies it into `DKRT_MF_EXT` (`bind`, from `Provider::start` after `info::bind`), where the
//! assembler reads it (`X_*` of `hooks-dt2.s`).
//!
//! The table is optional: an image without it (a device whose core/machines has no `src-pages.c`) leaves every word 0, and the hooks keep the one-page rule and
//! the registry's parameter mapping. A table is taken whole or not at all, so a half-usable one never reaches the hooks.
use core::ptr::{addr_of_mut, read_volatile, write_volatile};
use dkneeds::keys::name_hash;
use dkrt_abi::core_api::CoreApi;

/// `MFX1`.
pub const MAGIC: u32 = 0x4d46_5831;
pub const VERSION: u32 = 1;
/// Words of the table.
pub const WORDS: usize = 5;

/// Word indexes of the table (byte offset = 4 * index; `hooks-dt2.s` names the byte offsets `X_*`, and a test compares them).
pub mod w {
    pub const MAGIC: usize = 0;
    pub const VERSION: usize = 1;
    /// `mf_src_page_count(type) -> pages`
    pub const SRC_COUNT: usize = 2;
    /// `mf_src_apply_pages(view, pages) -> pages`
    pub const SRC_APPLY: usize = 3;
    /// `instrument_mod_param(type, mirror) -> code`, -1 for a slot that is not its own; 0 without core/instrument-play.
    pub const MOD_PARAM: usize = 4;
}

/// The byte offsets the assembler names (`.equ X_NAME, offset`), for the test that compares them with `w`.
pub const ASM_OFFSETS: &[(&str, usize)] = &[
    ("X_SRC_COUNT", w::SRC_COUNT),
    ("X_SRC_APPLY", w::SRC_APPLY),
    ("X_MOD_PARAM", w::MOD_PARAM),
];

/// The image table `machines_ext_info`, copied by `bind`; all 0 without it. The hooks of the service file read it by name (`X_*` of `hooks-dt2.s`), so it stays a named
/// static of the crate (`README.md`).
#[no_mangle]
pub static mut DKRT_MF_EXT: [u32; WORDS] = [0; WORDS];

/// Bind the image table into `DKRT_MF_EXT` (true), or clear it (false: the image has none this service can use). Never a refusal: the table is optional.
///
/// # Safety
/// Nothing reads `DKRT_MF_EXT` meanwhile (the hooks are declared after it).
pub unsafe fn bind<C: CoreApi>(core: &C) -> bool {
    bind_table(core, addr_of_mut!(DKRT_MF_EXT) as *mut u32)
}

/// Is `src` a table this service can use (magic, version, and both SRC page functions; the modulation word may be 0)?
unsafe fn usable(src: *const u32) -> bool {
    !src.is_null()
        && read_volatile(src.add(w::MAGIC)) == MAGIC
        && read_volatile(src.add(w::VERSION)) == VERSION
        && read_volatile(src.add(w::SRC_COUNT)) != 0
        && read_volatile(src.add(w::SRC_APPLY)) != 0
}

/// Copy the image table to `dst` (`WORDS` words), the magic last. True when the image has a usable table; otherwise every word of `dst` is 0 (a table is taken whole or not
/// at all).
///
/// # Safety
/// `dst` points at `WORDS` words.
pub unsafe fn bind_table<C: CoreApi>(core: &C, dst: *mut u32) -> bool {
    let src = core.info(name_hash("machines_ext_info"));
    let ok = usable(src);
    let mut i = 1;
    while i < WORDS {
        write_volatile(dst.add(i), if ok { read_volatile(src.add(i)) } else { 0 });
        i += 1;
    }
    write_volatile(dst, if ok { MAGIC } else { 0 });
    ok
}
