//! The seam with the image (`seam.toml`): the image table `instrument_image` of core/instrument-play, copied into `ip_img` (the
//! bss word array of `seam.s` that the thunks and the C code read), and the two vector tables the fills name.
use core::ptr::{read_volatile, write_volatile};
use dkrt_abi::core_api::CoreApi;
use dkrt_sdk::{E_NO_REGISTRY, OK};
use dkrt_svc_rt::name_hash;

#[rustfmt::skip]
#[path = "seam_gen.rs"]
mod gen;
pub use gen::{INFO, MAGIC, REQUIRED, VERSION, WORDS};

macro_rules! entries {
    ($($name:ident,)*) => {
        $(
            #[cfg(target_arch = "m68k")]
            extern "C" {
                pub fn $name();
            }
            #[cfg(not(target_arch = "m68k"))]
            pub extern "C" fn $name() {}
        )*
    };
}

// the vector tables of `seam.s` (data, named as functions so the fills can take their address)
entries! {
    instrument_vec_table,
    midi_vec_table,
}

#[cfg(target_arch = "m68k")]
extern "C" {
    /// The copy of the image table (`seam.s`, bss).
    static mut ip_img: [u32; WORDS];
    /// Fill the registry codes of src-mod.c from `ip_img`.
    fn ip_seam_rows();
}

/// Is `src` a table this file can use: magic, version and size of this layout, and every word of the adapter's own functions
/// and data set (an optional function of another adapter may be 0)?
///
/// # Safety
/// `src` is null or points at the image table.
pub unsafe fn usable(src: *const u32) -> bool {
    if src.is_null()
        || read_volatile(src) != MAGIC
        || read_volatile(src.add(1)) != VERSION
        || read_volatile(src.add(2)) != WORDS as u32
    {
        return false;
    }
    REQUIRED.iter().all(|&i| read_volatile(src.add(i)) != 0)
}

/// Copy the table to `dst` (`WORDS` words), the magic last.
///
/// # Safety
/// `src` is a usable table and `dst` points at `WORDS` words.
pub unsafe fn copy(src: *const u32, dst: *mut u32) {
    let mut i = 1;
    while i < WORDS {
        write_volatile(dst.add(i), read_volatile(src.add(i)));
        i += 1;
    }
    write_volatile(dst, MAGIC);
}

/// The service file's `setup` (the first step of `init`): bind the image table, or refuse the file (`E_NO_REGISTRY`: the image has
/// no core/instrument-play of this layout). The slots are filled by the loader after `init`, so no forwarder reaches the moved
/// code before this has run.
///
/// # Safety
/// Called once by `init`.
pub unsafe fn setup<C: CoreApi>(core: &C) -> i32 {
    let src = core.info(name_hash(INFO));
    if !usable(src) {
        return E_NO_REGISTRY;
    }
    #[cfg(target_arch = "m68k")]
    {
        copy(src, core::ptr::addr_of_mut!(ip_img) as *mut u32);
        ip_seam_rows();
    }
    OK
}
