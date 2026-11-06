//! Sticky machine type numbers (docs/design/module-sdk.md 11.5 D7, docs/design/digikit-registry.md "Machine keys").
//!
//! A run-time machine has no fixed type number. Its `.dkm` carries a machine key, `hash24 << 8` (the hash is the low 24 bits of FNV-1a 32 of
//! `<module id>:<machine id>`, the low byte is 0), and the runtime gives it a number in the run-time range when it
//! loads the module. A key with hash 0 is a pinned number (the low byte). The card remembers the
//! numbers in a map of up to 8 entries in the selection record's reserved bytes (`sel::Record::map`, entry = `hash24 << 8 | type`, 0 = empty), so
//! a machine keeps its number while it stays in the map, and a project that names the number keeps playing that machine.
//!
//! `plan` runs once per boot, before the first module loads: it reads the machine keys of every selected module (the staging buffer is
//! free then), assigns all numbers in load order and leaves the table `resolve` answers from. Doing it for the whole selection first is what
//! lets the eviction rule see which map entries belong to modules that load in this boot. The map is written back by the boot hook after the
//! loads, and only when it changed (`dirty`).
//!
//! Rules of the assignment, per machine in load order:
//! 1. the map has an entry for its hash and that number is not taken in this boot: use it (sticky; logged as rule 1);
//! 2. else the lowest number that no map entry references and this boot has not given out; record the entry (rule 0);
//! 3. else drop the lowest-index entry whose hash is not among this boot's machines (logged), take its number, record the entry (rule 0);
//! 4. two machines with the same key in one boot: the second takes a fresh number and is not recorded.
use crate::{dlog, loader};
use core::ptr::addr_of_mut;
use dkrt_sdk::sel::{Record, MAP_N};
use dkrt_sdk::sticky::{assign, clean, in_range, Event, MAX_KEYS};
use dkrt_sdk::*;
use dkrt_sdk::device::{DYN_TYPE_FIRST, DYN_TYPE_LAST};

/// The boot's assignment, peekable by the proof tools (`DKRT_ASSIGN`): `magic` "DKAS", `n` machines planned, `dirty` 1 when the map changed.
#[repr(C, align(4))]
pub struct State {
    magic: u32,
    n: usize,
    dirty: u32,
    /// The map as it is now (starts as the record's, cleaned).
    map: [u32; MAP_N],
    /// Per planned machine: the selection index of its module, its key, its number (0: none was free).
    idx: [u32; MAX_KEYS],
    key: [u32; MAX_KEYS],
    ty: [u32; MAX_KEYS],
}

#[no_mangle]
pub static mut DKRT_ASSIGN: State = State { magic: 0, n: 0, dirty: 0, map: [0; MAP_N], idx: [0; MAX_KEYS], key: [0; MAX_KEYS], ty: [0; MAX_KEYS] };

fn st() -> &'static mut State {
    unsafe { &mut *addr_of_mut!(DKRT_ASSIGN) }
}

/// Entries of the sticky map in use now (for CAPACITY).
pub fn map_used() -> u32 {
    let mut n = 0;
    let mut i = 0;
    while i < MAP_N {
        if st().map[i] != 0 {
            n += 1;
        }
        i += 1;
    }
    n
}

/// Assign the numbers of every machine of the selected modules. Call once, at the start of the boot load, with the record the boot started from.
pub unsafe fn plan(rec: &Record) {
    let a = st();
    a.magic = 0x444b_4153; // "DKAS"
    a.n = 0;
    a.map = clean(&rec.map);
    // the machines of the selection, in load order
    let mut k = 0;
    while k < rec.count as usize {
        let mut keys = [0u32; 8];
        let n = loader::machine_keys(rec.paths[k].as_ptr(), &mut keys);
        let mut i = 0;
        while i < n && a.n < MAX_KEYS {
            a.idx[a.n] = k as u32;
            a.key[a.n] = keys[i];
            a.ty[a.n] = 0;
            a.n += 1;
            i += 1;
        }
        k += 1;
    }
    let n = a.n;
    a.dirty = assign(&rec.map, &mut a.map, &a.key, &mut a.ty, n, &mut |ev: &Event| {
        let flags = if ev.error { logk::F_ERROR } else { dlog::INFO };
        dlog::ev(ev.kind, flags, ev.args);
    });
}

/// The type number for machine key `key` of the module being loaded (selection index `loader::BOOT_IDX`): the number `plan` assigned, a pinned
/// number as it is (when it is in the run-time range), 0 when there is none.
pub unsafe fn resolve(key: u32) -> u32 {
    if key >> 8 == 0 {
        return if in_range(key) { key } else { 0 }; // a pinned number (0: none)
    }
    let a = st();
    let at = core::ptr::read_volatile(addr_of_mut!(loader::BOOT_IDX));
    let mut i = 0;
    while i < a.n {
        if a.key[i] == key && a.idx[i] == at {
            return a.ty[i];
        }
        i += 1;
    }
    0
}

/// The map to write back when the boot changed it.
pub fn changed_map() -> Option<[u32; MAP_N]> {
    let a = st();
    if a.dirty != 0 {
        Some(a.map)
    } else {
        None
    }
}

/// V2 lookup uses the saved selection index, not the current BOOT_IDX. Keep this
/// borrowed snapshot on the UI task; no assignment-table pointer escapes.
pub unsafe fn resolve_instruments(
    section: &dkrt_sdk::instrument_section::Section<'_>,
    selection: u32,
) -> Result<dkrt_sdk::instrument_types::Resolved, dkrt_sdk::instrument_types::Error> {
    let a = st();
    if a.magic != 0x444b_4153 || a.n > MAX_KEYS || selection >= SEL_MAX as u32 {
        return Err(dkrt_sdk::instrument_types::Error::Map);
    }
    let context = dkrt_sdk::instrument_types::Context {
        selection,
        indices: &a.idx[..a.n],
        keys: &a.key[..a.n],
        types: &a.ty[..a.n],
        first: DYN_TYPE_FIRST,
        last: DYN_TYPE_LAST,
    };
    section.resolve_types(&context, |t| !crate::registry::type_is_free(t) || !crate::dsp::type_is_free(t))
}
