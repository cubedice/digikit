//! The resident part of the fork's persistent V2 instrument host: the statics the image adapters (core/instrument-play, core/track-keyboard) and the
//! tests read, which the coordinator of the system package writes through its views (`dkrt_abi::resident::InstrumentViews`,
//! `package/instrument.rs`). The image links `dkrt_instrument_type` and `dkrt_instrument_generation` by name, so they are resident functions.
use core::ptr::{addr_of, read_volatile};
use dkrt_sdk::instrument_provider::Binding;

/// The machine type number and the session generation of the published V2 instrument; 0 and 0 while none is published.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_WORDS: [u32; 2] = [0; 2];
/// UI-owned read-only diagnostics of a normal-file V2 load: phase, module record, terminal load result, last progress or failure.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_FILE: [u32; 4] = [0; 4];
/// Emulator-only bootstrap diagnostic: phase (idle/acquire/bind/release/bound/blocked), token, start tick.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_SESSION: [u32; 3] = [0; 3];
/// The emulator-only lifecycle probe's status words (test/instrument-load).
#[no_mangle]
pub static mut DKRT_INSTRUMENT_LOAD: [u32; 6] = [0; 6];
/// Read-only diagnostic header of the provider binding (attempted, symbol-presence mask, bound) and the private provider set after it. Never reset for a
/// DSP-only fault; only a whole ColdFire boot creates a fresh namespace.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_BINDING: Binding = Binding::new();

/// The machine type of the published V2 instrument, 0 while none is (the image adapters' test of a CANOPY track).
#[no_mangle]
pub unsafe extern "C" fn dkrt_instrument_type() -> u32 {
    read_volatile(addr_of!(DKRT_INSTRUMENT_WORDS[0]))
}

/// The session generation of the published V2 instrument, 0 while none is.
#[no_mangle]
pub unsafe extern "C" fn dkrt_instrument_generation() -> u32 {
    read_volatile(addr_of!(DKRT_INSTRUMENT_WORDS[1]))
}
