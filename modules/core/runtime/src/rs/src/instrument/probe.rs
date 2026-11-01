//! Emulator-only lifecycle probe. Never exposes a machine or a MIDI route.
//! Holds the lease from preparation through acknowledged detach and storage
//! release, so no CF module admission can reuse the temporary ledger slot.
use super::{Owner, LinkProgress, Progress};
use core::ptr::{addr_of_mut, read_volatile};
use dkrt_sdk::{instrument_admission::{Limits, Range}, instrument_descriptor,
    instrument_load::Command, instrument_registry::Ticket,
    instrument_section::Section};
extern "C" { fn instrument_load_probe() -> *const u32; }
pub(super) fn present() -> bool {
    core::hint::black_box(instrument_load_probe as usize) != 0
}
static mut IMAGE: [u8; 256] = [0; 256];
static mut TICKET: Option<Ticket> = None;
#[no_mangle]
pub static mut DKRT_INSTRUMENT_LOAD: [u32; 6] = [0; 6];
const SLOT: usize = crate::MAX_MODULES - 1;

pub(super) unsafe fn tick(owner: &mut Owner, link: LinkProgress, now: u32) {
    if !present() { return; }
    let input = instrument_load_probe();
    let request = read_volatile(input);
    let status = &mut *addr_of_mut!(DKRT_INSTRUMENT_LOAD);
    if status[0] == 6 || status[0] == 9 { return; }
    // Use the production allocator bounds and shared legacy/V2 reservation table.
    let Some(mut ledger) = crate::dsp::instrument_ledger() else {
        status[0] = 9; return;
    };
    if status[0] == 0 {
        if request != 1 { return; }
        if crate::st().modules[SLOT].used != 0 || ledger.lengths[SLOT] != 0
            || owner.request_link(now, 2000).is_err() {
            status[0] = 9; return;
        }
        status[0] = 1;
    } else if status[0] == 1 && link == LinkProgress::Owned {
        core::ptr::write_volatile(&mut status[3], 1);
        let len = read_volatile(input.add(1)) as usize;
        if len > 256 { status[0] = 9; return; }
        let bytes = core::slice::from_raw_parts(input.add(3).cast::<u8>(), len);
        let limits = instrument_descriptor::Limits { state_bytes: 64,
            cycles_process: 100, cycles_reset: 100, call_depth: 2, stack_bytes: 32 };
        let Ok(section) = Section::parse(bytes, 1, limits) else { status[0] = 9; return; };
        core::ptr::write_volatile(&mut status[3], 2);
        let size = section.memory_request().image_bytes as usize;
        if size > 256 { status[0] = 9; return; }
        let output = &mut (&mut *addr_of_mut!(IMAGE))[..size];
        let limits = Limits { l2: Range { start: 0x20000000, end: 0x20080000 },
            reset_frame: 1000, reset_overhead: 500, process_total: 100,
            call_depth: 2, stack_bytes: 32 };
        core::ptr::write_volatile(&mut status[3], 3);
        let Ok(plan) = owner.admit(SLOT as u32, &section, &[20], &limits,
            &mut ledger, output, |_| None) else { status[0] = 9; return; };
        core::ptr::write_volatile(&mut status[3], 4);
        TICKET = Some(plan.ticket());
        status[1] = ledger.base + ledger.offsets[SLOT];
        status[2] = plan.rows()[0].unwrap().words[4];
        status[5] = ledger.lengths[SLOT];
        if owner.begin(plan, output, now, 2000).is_err() { status[0] = 9; return; }
        status[3] = 5;
        status[0] = 2;
    } else if status[0] == 2 || status[0] == 4 {
        match owner.tick(now) {
            Progress::Complete { command: Command::Publish(ticket), recovered: None }
                if status[0] == 2 && Some(ticket) == TICKET => {
                    status[0] = 3; status[4] = owner.loads().lookup(20).is_some() as u32;
                }
            Progress::Complete { command: Command::Release(ticket), .. } if Some(ticket) == TICKET => {
                if owner.release_memory(ticket, &mut ledger).is_err()
                    || owner.request_link_release(now, 2000).is_err() { status[0] = 9; return; }
                status[5] = ledger.lengths[SLOT]; status[0] = 5;
            }
            Progress::Blocked(cause) => {
                status[3] = 100 + cause as u32;
                status[0] = 9;
            }
            Progress::Pending => {}
            _ => { status[0] = 9; }
        }
    } else if status[0] == 3 && request == 2 {
        if owner.retire(TICKET.unwrap(), now, 2000).is_err() { status[0] = 9; return; }
        status[4] = owner.loads().lookup(20).is_some() as u32;
        status[0] = 4;
    } else if status[0] == 5 && link == LinkProgress::Released {
        status[0] = 6;
    }
}
