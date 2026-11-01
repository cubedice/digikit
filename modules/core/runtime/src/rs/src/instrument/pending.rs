//! Retained normal-file V2 load, serialized on the owning UI task.
//! A pending CF record is occupied but unpublished; no staging-buffer pointer
//! survives boot. Uncertain DSP failures retain the entire record until reboot.
use super::{Owner, LinkProgress, Progress};
use core::ptr::addr_of_mut;
use dkrt_sdk::{*, instrument_admission::{Limits, Range}, instrument_descriptor,
    instrument_load::Command, instrument_registry::Ticket, instrument_section::Section};

pub(crate) struct Pending {
    pub rec: usize,
    pub selection: u32,
    pub reg: u32,
    pub reg_size: u32,
    pub section: u32,
    pub section_size: u32,
    pub output: u32,
    pub output_size: u32,
    pub init: u32,
    pub menu: u32,
    pub mask: u32,
    pub fns: [u32; SLOTS],
    phase: u32,
    ticket: Option<Ticket>,
    result: i32,
    reported: bool,
}
static mut PENDING: Option<Pending> = None;
// UI-owned read-only diagnostics: phase, module record, terminal load result, last progress/failure.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_FILE: [u32; 4] = [0; 4];

// Admission policy is supplied only by the trusted firmware adapter, never a
// DKM. Eight words: magic, version, tracks, process, reset, depth, stack, state.
extern "C" { fn instrument_runtime_policy() -> *const u32; }
pub(crate) unsafe fn policy() -> Option<[u32; 8]> {
    if core::hint::black_box(instrument_runtime_policy as usize) == 0 { return None; }
    let ptr = instrument_runtime_policy();
    if ptr.is_null() { return None; }
    let mut words = [0; 8];
    for (i, value) in words.iter_mut().enumerate() { *value = core::ptr::read_volatile(ptr.add(i)); }
    if words[0] != 0x49525031 || words[1] != 1 || words[2] != 1 ||
        words[3] == 0 || words[3] > 100000 || words[4] == 0 || words[4] > 10000 ||
        words[5] == 0 || words[5] > 8 || words[6] == 0 || words[6] > 128 ||
        words[7] == 0 || words[7] > 4096 { return None; }
    Some(words)
}
pub(crate) unsafe fn descriptor_limits() -> Option<instrument_descriptor::Limits> {
    let p = policy()?;
    Some(instrument_descriptor::Limits { state_bytes: p[7], cycles_process: p[3],
        cycles_reset: p[4], call_depth: p[5], stack_bytes: p[6] })
}
pub(crate) unsafe fn occupied() -> bool { (&*core::ptr::addr_of!(PENDING)).is_some() }
pub(crate) unsafe fn slots_busy(mask: u32) -> bool {
    (&*core::ptr::addr_of!(PENDING)).as_ref().is_some_and(|p| p.mask & mask != 0)
}
pub(crate) unsafe fn queue(mut p: Pending) {
    p.phase = 0; p.ticket = None;
    *addr_of_mut!(PENDING) = Some(p);
}
impl Pending {
    pub(crate) fn new(rec: usize, selection: u32, reg: u32, reg_size: u32,
        section: u32, section_size: u32, output: u32, output_size: u32,
        init: u32, menu: u32, mask: u32, fns: [u32; SLOTS]) -> Self {
        Self { rec, selection, reg, reg_size, section, section_size, output,
            output_size, init, menu, mask, fns, phase: 0, ticket: None, result: E_DSP_XFER, reported: false }
    }
    unsafe fn discard(&mut self, result: i32) {
        // Only before admission or after release_memory, never on uncertainty.
        crate::arena::free(crate::st().modules[self.rec].base as *mut u8);
        crate::st().modules[self.rec].used = 0;
        (*addr_of_mut!(crate::DKRT_MENUS))[self.rec] = 0;
        self.report(result);
        crate::table::check();
    }
    unsafe fn report(&mut self, result: i32) {
        self.result = result;
        if !self.reported {
            crate::boot::note_result(self.selection, result);
            self.reported = true;
        }
    }
}
pub(crate) unsafe fn blocked(cause: u32) {
    if let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() {
        p.phase = 9;
        if p.result == OK || !p.reported { p.report(E_DSP_XFER); }
        if (*addr_of_mut!(DKRT_INSTRUMENT_FILE))[0] != 9 {
            *addr_of_mut!(DKRT_INSTRUMENT_FILE) = [p.phase, p.rec as u32, p.result as u32, cause];
        }
    }
}
pub(crate) unsafe fn tick(owner: &mut Owner, link: LinkProgress, now: u32) {
    let before = (&*core::ptr::addr_of!(PENDING)).as_ref().map_or(0, |p| p.phase);
    tick_inner(owner, link, now);
    if let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() {
        if p.phase == 9 && !p.reported { p.report(p.result); }
        if (*addr_of_mut!(DKRT_INSTRUMENT_FILE))[0] != 9 {
            *addr_of_mut!(DKRT_INSTRUMENT_FILE) = [p.phase, p.rec as u32, p.result as u32, before];
        }
    }
}
unsafe fn tick_inner(owner: &mut Owner, link: LinkProgress, now: u32) {
    let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() else { return; };
    if p.phase == 6 || p.phase == 9 || p.phase == 3 { return; }
    if p.phase == 0 {
        if owner.request_link(now, 2000).is_err() { p.phase = 9; return; }
        p.phase = 1; return;
    }
    let Some(mut ledger) = crate::dsp::instrument_ledger() else { p.phase = 9; return; };
    if p.phase == 1 && link == LinkProgress::Owned {
        let result = prepare(owner, p, &mut ledger, now);
        if result != OK {
            if p.ticket.is_some() { p.phase = 9; return; }
            p.discard(result);
            p.phase = if owner.request_link_release(now, 2000).is_ok() { 5 } else { 9 };
        }
    } else if p.phase == 2 || p.phase == 4 {
        match owner.tick(now) {
            Progress::Complete { command: Command::Publish(ticket), recovered: None }
                if p.phase == 2 && Some(ticket) == p.ticket => {
                let old = crate::loader::BOOT_IDX;
                crate::loader::BOOT_IDX = p.selection;
                let result = crate::loader::publish_cf(p.rec, p.reg as *const u8,
                    p.reg_size, p.init, p.mask, &p.fns);
                crate::loader::BOOT_IDX = old;
                if result == OK {
                    (*addr_of_mut!(crate::DKRT_MENUS))[p.rec] = p.menu;
                    p.report(OK);
                    crate::table::check();
                    // Release only the link: published code/state remain owned.
                    p.phase = if owner.request_link_release(now, 2000).is_ok() { 7 } else { 9 };
                } else {
                    p.report(result);
                    p.phase = if owner.retire(ticket, now, 2000).is_ok() { 4 } else { 9 };
                }
            }
            Progress::Complete { command: Command::Release(ticket), .. }
                if Some(ticket) == p.ticket => {
                if owner.release_memory(ticket, &mut ledger).is_err() { p.phase = 9; return; }
                p.discard(p.result);
                p.phase = if owner.request_link_release(now, 2000).is_ok() { 5 } else { 9 };
            }
            Progress::Pending => {}
            Progress::Blocked(cause) => {
                *addr_of_mut!(DKRT_INSTRUMENT_FILE) = [9, p.rec as u32, p.result as u32, 100 + cause as u32];
                p.phase = 9;
            }
            _ => { p.phase = 9; }
        }
    } else if p.phase == 5 && link == LinkProgress::Released { p.phase = 6; }
    else if p.phase == 7 && link == LinkProgress::Released { p.phase = 3; }
}
unsafe fn prepare(owner: &mut Owner, p: &mut Pending, ledger: &mut instrument_load::Ledger<'_>, now: u32) -> i32 {
    let Some(policy) = policy() else { return E_CONTRACT; };
    let bytes = core::slice::from_raw_parts(p.section as *const u8, p.section_size as usize);
    let Some(bounds) = descriptor_limits() else { return E_CONTRACT; };
    let Ok(section) = Section::parse(bytes, crate::device(), bounds) else { return E_FORMAT; };
    let Ok(types) = crate::assign::resolve_instruments(&section, p.selection) else { return E_FORMAT; };
    let limits = Limits { l2: Range { start: 0x20000000, end: 0x20080000 },
        reset_frame: 20000, reset_overhead: 1000, process_total: policy[3],
        call_depth: policy[5], stack_bytes: policy[6] };
    let output = core::slice::from_raw_parts_mut(p.output as *mut u8, p.output_size as usize);
    let Ok(plan) = owner.admit(p.rec as u32, &section, types.as_slice(), &limits,
        ledger, output, |_| None) else { return E_DSP_XFER; };
    p.ticket = Some(plan.ticket());
    if owner.begin(plan, output, now, 2000).is_err() { return E_DSP_XFER; }
    p.phase = 2;
    OK
}
