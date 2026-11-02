// Execute the production runtime store and codec with an in-memory block device.
#![allow(dead_code)]
use dkrt_sdk::{proto::*, *};
use std::sync::atomic::{AtomicI32, AtomicU32, Ordering};
#[path = "../../../modules/core/runtime/src/rs/src/settings.rs"]
mod settings;
static ERROR: AtomicI32 = AtomicI32::new(0);
static WRITES: AtomicU32 = AtomicU32::new(0);
static mut CARD: [u8; 65536] = [0; 65536];
pub fn init_once() {}
pub fn fw_blocks() -> Option<u32> {
    None
}
pub mod card {
    pub fn state() -> u32 {
        0
    }
}
pub mod dlog {
    pub const INFO: u16 = 0;
    pub fn ev(_: u16, _: u16, _: [u32; 5]) {}
}
pub mod store {
    pub fn open() -> Result<(), i32> {
        Err(dkrt_sdk::E_NOT_FOUND)
    }
    pub fn layout() -> dkrt_sdk::store::Layout {
        dkrt_sdk::store::Layout {
            first: 0,
            sectors: 128,
        }
    }
}
pub unsafe fn block_read(sector: u32, bytes: u32, out: *mut u8) -> i32 {
    let offset = sector as usize * 512;
    if offset + bytes as usize > 65536 {
        return E_RANGE;
    }
    std::ptr::copy_nonoverlapping(
        std::ptr::addr_of!(CARD).cast::<u8>().add(offset),
        out,
        bytes as usize,
    );
    OK
}
pub unsafe fn block_write(sector: u32, bytes: u32, input: *const u8) -> i32 {
    WRITES.fetch_add(1, Ordering::Relaxed);
    let error = ERROR.load(Ordering::Relaxed);
    if error != 0 {
        return error;
    }
    let offset = sector as usize * 512;
    if offset + bytes as usize > 65536 {
        return E_RANGE;
    }
    std::ptr::copy_nonoverlapping(
        input,
        std::ptr::addr_of_mut!(CARD).cast::<u8>().add(offset),
        bytes as usize,
    );
    OK
}
fn main() {
    let arg = std::env::args().nth(1).unwrap();
    let error = if arg == "readonly" {
        E_CARD_RO
    } else if arg == "range" {
        E_RANGE
    } else {
        E_WRITE
    };
    unsafe {
        settings::load();
        settings::put(b"canopy/preset/1", b"old");
        let first = settings::flush_if_dirty();
        ERROR.store(error, Ordering::Relaxed);
        settings::put(b"canopy/preset/1", b"new");
        let mut failure = 0;
        // Transient errors exhaust the automatic retry budget; permanent ones
        // stop immediately. Both leave RAM newer than the card.
        for _ in 0..3 {
            failure = settings::flush_if_dirty();
        }
        let dirty = settings::DKRT_SET.dirty;
        let before = WRITES.load(Ordering::Relaxed);
        // Same-value put is deliberately a no-op in the production codec.
        settings::put(b"canopy/preset/1", b"new");
        ERROR.store(0, Ordering::Relaxed);
        let retry = settings::flush_if_dirty();
        let after = WRITES.load(Ordering::Relaxed);
        let clean = settings::flush_if_dirty();
        let final_writes = WRITES.load(Ordering::Relaxed);
        settings::load();
        let mut b = [0u8; 3];
        let length = settings::get(b"canopy/preset/1", &mut b);
        println!("{{\"first\":{first},\"failure\":{failure},\"dirty\":{dirty},\"retry\":{retry},\"newWrites\":{},\"clean\":{clean},\"cleanWrites\":{},\"length\":{length},\"value\":\"{}\"}}",
            after-before,final_writes-after,std::str::from_utf8(&b).unwrap());
    }
}
