// The optional phrase symbols of the image (core/phrase-ui, core/phrase-control), for a host test binary: `include!` this file once per binary.
// With them defined `phrase::available()` is true, as in an image with the phrase adapters.
#[no_mangle]
pub unsafe extern "C" fn phrase_bind(_: u32, _: u32, _: u32, _: *const u8, _: *const u8) {}
#[no_mangle]
pub unsafe extern "C" fn phrase_control_tick() {}
#[no_mangle]
pub unsafe extern "C" fn phrase_ui_tick() {}
