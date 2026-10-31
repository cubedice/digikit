//! Bind optional V2 providers once on the UI task. This does not acquire the
//! DSP link, enable capabilities, or admit any module to the V2 loader.
use core::ptr::addr_of_mut;
use dkrt_sdk::instrument_provider::Binding;

extern "C" {
    // Optional patchkit relocations resolve to zero when their module is absent.
    fn dl_claim();
    fn dl_release();
    fn dl_send_owned();
    fn dl_poll_tagged();
    fn mi_registry_reply();
}

/// Read-only diagnostic header: attempted, symbol-presence mask, bound. The
/// private provider set follows it. Never reset this state for a DSP-only fault;
/// only a whole ColdFire boot creates a fresh namespace.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_BINDING: Binding = Binding::new();

pub unsafe fn bind_once() {
    let binding = &mut *addr_of_mut!(DKRT_INSTRUMENT_BINDING);
    if binding.diagnostics()[0] != 0 {
        return;
    }
    // The black boxes preserve optional-zero checks even though Rust normally
    // assumes that a declared function's address is nonzero.
    binding.bind_once([
        core::hint::black_box(dl_claim as usize),
        core::hint::black_box(dl_release as usize),
        core::hint::black_box(dl_send_owned as usize),
        core::hint::black_box(dl_poll_tagged as usize),
        core::hint::black_box(mi_registry_reply as usize),
    ]);
}
