//! The image table `machines_info` of `core/machines` (its `[[info]]`), as the machines service file (`machines.dks`) uses it.
//!
//! The table holds what only the patched image knows: the registry rows and counts, `mf_ensure`, the adapter's native page functions, the slot map and the page rows.
//! The firmware (function addresses, where each hook continues in the stock code) is not in it: the service file names it and `patchkit bind` resolves it for the user's firmware
//! (the `dk_fw_`, `dk_exit_`, `dk_ret_` and `dk_stock_` imports of docs/PATCHING.md "Firmware references"). The file reads the table through `CoreApi::info` and copies it into its
//! own bss (`bind_table`, from `setup`), where its hooks (`hooks-dt2.s`, `hooks-dn2.s`) read it as `MF_TAB`. One layout for both devices; a word a device does not use is 0.
//!
//! The layout (`w`) is `dkrt_svc_machines::info`: the crate reads the native page functions from the table too. The words are big endian in the image (ColdFire is big endian, so a read of the word is the value).
use core::ptr::{read_volatile, write_volatile};
use dkrt_abi::core_api::CoreApi;
use dkrt_sdk::{E_NO_REGISTRY, OK};
use dkrt_svc_rt::name_hash;

pub use dkrt_svc_machines::info::{w, MAGIC, VERSION, WORDS};

/// The byte offsets the assembler files name (`.equ T_NAME, offset`), for the test that compares them with `w`.
pub const ASM_OFFSETS: &[(&str, usize)] = &[
    ("T_FIRST", w::FIRST),
    ("T_COUNT", w::COUNT),
    ("T_ROWS", w::ROWS),
    ("T_ENSURE", w::ENSURE),
    ("T_N_PAGE_COUNT", w::N_PAGE_COUNT),
    ("T_N_DESCRIPTOR", w::N_DESCRIPTOR),
    ("T_N_CODE", w::N_CODE),
    ("T_N_DRAW", w::N_DRAW),
    ("T_N_OVERLAY", w::N_OVERLAY),
    ("T_SLOTMAP", w::SLOTMAP),
    ("T_PGROWS", w::PGROWS),
    ("T_PGN", w::PGN),
];

/// The image table `machines_info`, copied by `setup`; the hooks read it (`T_*` of the assembler files).
#[no_mangle]
pub static mut MF_TAB: [u32; WORDS] = [0; WORDS];

/// The image table `machines_ext_info` (Digitakt II, `mf_ext_info` of core/machines): the adapter functions the hooks `mf_srcpage` and `mf_typearg` call.
/// Words: magic, version, then the functions at the byte offsets `X_*` of `hooks-dt2.s`. An image without the table leaves every word 0, and the hooks
/// keep the one-page rule and the registry's parameter mapping.
pub mod x {
    pub const MAGIC: usize = 0;
    pub const VERSION: usize = 1;
    /// `mf_src_page_count(type) -> pages`
    pub const SRC_COUNT: usize = 2;
    /// `mf_src_apply_pages(view, pages) -> pages`
    pub const SRC_APPLY: usize = 3;
    /// `instrument_mod_param(type, mirror) -> code`, -1: not its slot; 0 without core/instrument-play
    pub const MOD_PARAM: usize = 4;
}
pub const EXT_MAGIC: u32 = 0x4d46_5831; // "MFX1"
pub const EXT_VERSION: u32 = 1;
pub const EXT_WORDS: usize = 5;

/// The byte offsets of `MF_EXT` the assembler names (`.equ X_NAME, offset`).
pub const ASM_EXT_OFFSETS: &[(&str, usize)] = &[("X_SRC_COUNT", x::SRC_COUNT), ("X_SRC_APPLY", x::SRC_APPLY), ("X_MOD_PARAM", x::MOD_PARAM)];

/// The image table `machines_ext_info`, copied by `setup`; all 0 without it.
#[no_mangle]
pub static mut MF_EXT: [u32; EXT_WORDS] = [0; EXT_WORDS];

/// The service file's `setup` (the first step of `init`): bind the image table into `MF_TAB`, or the file is refused (`E_NO_REGISTRY`: no usable `core/machines` in the image).
/// The optional table `machines_ext_info` is bound after it.
pub unsafe fn setup<C: CoreApi>(core: &C) -> i32 {
    let r = bind_table(core, core::ptr::addr_of_mut!(MF_TAB) as *mut u32);
    if r == OK {
        bind_ext(core, core::ptr::addr_of_mut!(MF_EXT) as *mut u32);
    }
    r
}

/// Copy the image table `machines_ext_info` to `dst` (`EXT_WORDS` words). True when the image has one of this version with both SRC page functions;
/// otherwise `dst` is all 0 (a table is taken whole or not at all).
///
/// # Safety
/// `dst` points at `EXT_WORDS` words.
pub unsafe fn bind_ext<C: CoreApi>(core: &C, dst: *mut u32) -> bool {
    let src = core.info(name_hash("machines_ext_info"));
    let ok = !src.is_null()
        && read_volatile(src.add(x::MAGIC)) == EXT_MAGIC
        && read_volatile(src.add(x::VERSION)) == EXT_VERSION
        && read_volatile(src.add(x::SRC_COUNT)) != 0
        && read_volatile(src.add(x::SRC_APPLY)) != 0;
    let mut i = 0;
    while i < EXT_WORDS {
        write_volatile(dst.add(i), if ok { read_volatile(src.add(i)) } else { 0 });
        i += 1;
    }
    ok
}

/// The table of the image, or null: the image has no `core/machines`.
unsafe fn image<C: CoreApi>(core: &C) -> *const u32 {
    core.info(name_hash("machines_info"))
}

/// Is `src` a table this service can use (magic, version, the device the core runs on, and the words the hooks cannot do without)?
unsafe fn usable<C: CoreApi>(core: &C, src: *const u32) -> bool {
    !src.is_null()
        && read_volatile(src.add(w::MAGIC)) == MAGIC
        && read_volatile(src.add(w::VERSION)) == VERSION
        && read_volatile(src.add(w::DEVICE)) == core.device_id()
        && read_volatile(src.add(w::ENSURE)) != 0
        && read_volatile(src.add(w::ROWS)) != 0
}

/// Copy the image table to `dst` (`WORDS` words), the magic last: a table is usable only when every word is in. `E_NO_REGISTRY` when the image has no usable table.
///
/// # Safety
/// `dst` points at `WORDS` words.
pub unsafe fn bind_table<C: CoreApi>(core: &C, dst: *mut u32) -> i32 {
    let src = image(core);
    if !usable(core, src) {
        return E_NO_REGISTRY;
    }
    let mut i = 1;
    while i < WORDS {
        write_volatile(dst.add(i), read_volatile(src.add(i)));
        i += 1;
    }
    write_volatile(dst, MAGIC);
    OK
}
