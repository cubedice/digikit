//! The optional functions of the image that the effect pages use, found by name in the image's info table (each an `[[info]]` entry: the function itself) when the file
//! starts (`Fx::start`, after `bind`): the page painter of core/loop-display, which the hook `fxp_graph` calls through `DKRT_FXP_PAINT`, and the holder of an effect's
//! values (core/loop-control). The service file has no link to the image, so it has no other way to reach them. An image without them gives 0 for each: every page has
//! the stock paint and every effect's values are the kit's.

use super::*;
use core::ptr::{addr_of_mut, read_volatile, write_volatile};
use dkrt_abi::core_api::CoreApi;

/// The name of the page painter: `int loop_ui_paint(view, bitmap, page)` of core/loop-display, not 0 when it painted the page (the hook then skips the stock paint).
pub const PAINT_HASH: u32 = fnv1a(b"loop_ui_paint");

/// The page painter of the image, or 0.
pub unsafe fn page_painter<C: CoreApi>(core: &C) -> u32 {
    core.info(PAINT_HASH) as u32
}

/// Put the page painter (or 0) into `DKRT_FXP_PAINT`, the word the hook `fxp_graph` calls before the stock paint of an FX page.
pub unsafe fn painter_setup<C: CoreApi>(core: &C) {
    write_volatile(addr_of_mut!(DKRT_FXP_PAINT), page_painter(core));
}

/// The name of the image function that holds an effect's values outside the kit: `int fx_held_values(u32 effect_id, u32 page, u16 *raw)`, an
/// `[[info]]` entry (the function itself; core/loop-control has it for the loop effect). `page` is the native FX page of the effect's row. It returns 1
/// and fills the raw values of the effect's knobs, on the kit's scale (0 to 0x7f00), when it holds the values of the effect with this ID, else 0. For such an effect the
/// kit's values are never read: its knobs are the adapter's and its MIX stays the record's default, so its values are the same in every kit and do not change at a pattern
/// change.
pub const HELD_HASH: u32 = fnv1a(b"fx_held_values");
static mut HELD: usize = 0;

/// Find the function (`Fx::start`); 0 when the image has none: every effect's values are the kit's.
pub unsafe fn held_setup<C: CoreApi>(core: &C) {
    set_held(core.info(HELD_HASH) as usize);
}

/// Set the function's address (`held_setup`; a host test gives one of its own).
pub unsafe fn set_held(f: usize) {
    write_volatile(addr_of_mut!(HELD), f);
}

/// The raw values an adapter holds for the effect `id`, whose page is `page`, or `None` when no adapter holds them.
pub(super) unsafe fn held_values(id: u32, page: u32) -> Option<[u16; 8]> {
    let f = read_volatile(addr_of_mut!(HELD));
    if f == 0 || id == 0 {
        return None;
    }
    let call: unsafe extern "C" fn(u32, u32, *mut u16) -> i32 = core::mem::transmute(f);
    let mut raw = [0u16; 8];
    if call(id, page, raw.as_mut_ptr()) == 1 {
        Some(raw)
    } else {
        None
    }
}

/// The raw value that word `j` of a row takes: the adapter's for a knob of an effect whose values it holds (`held`), the record's default `def` for that
/// effect's MIX, and the kit's (`kit`, read only then) for every other effect.
pub(super) fn source_raw(
    held: Option<&[u16; 8]>,
    j: usize,
    def: i32,
    kit: impl FnOnce() -> i32,
) -> i32 {
    match held {
        Some(_) if j == WORDS - 1 => def,
        Some(raw) => raw[j] as i32,
        None => kit(),
    }
}
