//! The image table `relay_image` of `core/relay-track` (its `[[info]]`, `src/seam-table.s`), as the service file binds it: the
//! layout `seam.toml` gives (generated: `seam_gen.rs`), the check of a table and its copy. The service file's `setup` reads the
//! table through `CoreApi::info`, checks it with [`usable`] and copies it into its bss (`rt_img`) with [`copy`].
use core::ptr::{read_volatile, write_volatile};

#[rustfmt::skip]
#[path = "seam_gen.rs"]
mod gen;
pub use gen::{INFO, MAGIC, REQUIRED, VERSION, WORDS};

/// Is `src` a table the file can use: magic, version and size of this layout, and every word of the adapter's own functions
/// and data set? An optional function of another adapter may be 0.
///
/// # Safety
/// `src` is null or points at `WORDS` readable words.
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

/// Copy the table to `dst` (`WORDS` words), the magic last: a reader that sees the magic sees every word.
///
/// # Safety
/// `src` is a usable table and `dst` points at `WORDS` writable words.
pub unsafe fn copy(src: *const u32, dst: *mut u32) {
    let mut i = 1;
    while i < WORDS {
        write_volatile(dst.add(i), read_volatile(src.add(i)));
        i += 1;
    }
    write_volatile(dst, MAGIC);
}
