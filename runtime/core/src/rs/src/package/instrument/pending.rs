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
    pub image_size: u32,
    /// The copy of the file's keys table (`complete` reads the machine keys from it).
    pub keys: u32,
    pub keys_size: u32,
    /// The store file the loader staged for this load, and where its DSP section is in it: `prepare` stages the file again and reads the section there (the
    /// arena block holds no copy of it).
    pub file: Option<dkrt_abi::storage::StoreFile>,
    pub section_off: u32,
    pub section_size: u32,
    pub output: u32,
    pub output_size: u32,
    pub init: u32,
    pub tick: u32,
    pub menu: u32,
    phase: u32,
    ticket: Option<Ticket>,
    result: i32,
    reported: bool,
    type_no: u32,
    was_live: bool,
}
static mut PENDING: Option<Pending> = None;
/// The machine type and the session generation of the published V2 instrument (0, 0 while none is): what the resident `dkrt_instrument_type` and
/// `dkrt_instrument_generation` answer (`ui_pass` writes them each UI pass).
pub(crate) unsafe fn published() -> (u32, u32) {
    if let Some(p) = (&*core::ptr::addr_of!(PENDING)).as_ref() {
        if p.phase == 3 {
            return (p.type_no, p.ticket.map_or(0, |t| t.generation()));
        }
    }
    (0, 0)
}

// Admission policy is supplied only by the trusted firmware adapter, never a
// DKM. Eight words: magic, version, tracks, process, reset, depth, stack, state.
// Tracks (1..16) is the admitted instance count; process and state are the
// per-instance ceilings, so the total budget and allocation scale with it.
pub(crate) unsafe fn policy() -> Option<[u32; 8]> {
    let Some(a) = super::present(super::links().instrument_runtime_policy) else { return None; };
    let ptr = (core::mem::transmute::<usize, unsafe extern "C" fn() -> *const u32>(a))();
    if ptr.is_null() { return None; }
    let mut words = [0; 8];
    for (i, value) in words.iter_mut().enumerate() { *value = core::ptr::read_volatile(ptr.add(i)); }
    if words[0] != 0x49525031 || words[1] != 1 || !(1..=16).contains(&words[2]) ||
        words[3] == 0 || words[3] > 100000 || words[4] == 0 || words[4] > 10000 ||
        words[5] == 0 || words[5] > 8 || words[6] == 0 || words[6] > 128 ||
        words[7] == 0 || words[7] > 4096 { return None; }
    Some(words)
}
pub(crate) unsafe fn descriptor_limits() -> Option<instrument_descriptor::Limits> {
    let p = policy()?;
    // The descriptor's allocation covers every instance at the 8-byte stride.
    let state_bytes = ((p[7] + 7) & !7).checked_mul(p[2])?;
    Some(instrument_descriptor::Limits { state_bytes, cycles_process: p[3],
        cycles_reset: p[4], call_depth: p[5], stack_bytes: p[6] })
}
pub(crate) unsafe fn occupied() -> bool { (&*core::ptr::addr_of!(PENDING)).is_some() }
pub(crate) unsafe fn queue(mut p: Pending) {
    p.phase = 0; p.ticket = None;
    *addr_of_mut!(PENDING) = Some(p);
}
impl Pending {
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn new(rec: usize, selection: u32, image_size: u32, keys: u32, keys_size: u32,
        file: Option<dkrt_abi::storage::StoreFile>, section_off: u32, section_size: u32, output: u32, output_size: u32,
        init: u32, tick: u32, menu: u32) -> Self {
        Self { rec, selection, image_size, keys, keys_size, file, section_off, section_size, output,
            output_size, init, tick, menu, phase: 0, ticket: None, result: E_DSP_XFER, reported: false, type_no: 0, was_live: false }
    }
    unsafe fn discard(&mut self, result: i32) {
        // Only before admission or after release_memory, never on uncertainty.
        let st = crate::package::resident::state::st();
        crate::package::resident::arena::free(dkrt_abi::dev_addr::DevAddr::new(st.modules[self.rec].base));
        st.modules[self.rec].used = 0;
        (*crate::package::resident::state::menus())[self.rec] = 0;
        self.report(result);
        crate::package::invariants::check();
    }
    unsafe fn report(&mut self, result: i32) {
        self.result = result;
        if !self.reported {
            crate::package::resident::boot::report_dsp(self.selection, result);
            self.reported = true;
        }
    }
}
pub(crate) unsafe fn blocked(cause: u32) {
    if let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() {
        p.phase = 9;
        if p.result == OK || !p.reported { p.report(E_DSP_XFER); }
        if super::file()[0] != 9 {
            *super::file() = [p.phase, p.rec as u32, p.result as u32, cause];
        }
    }
}
pub(crate) unsafe fn tick(owner: &mut Owner, link: LinkProgress, now: u32) {
    let before = (&*core::ptr::addr_of!(PENDING)).as_ref().map_or(0, |p| p.phase);
    tick_inner(owner, link, now);
    if let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() {
        if p.phase == 9 && !p.reported { p.report(p.result); }
        if super::file()[0] != 9 {
            *super::file() = [p.phase, p.rec as u32, p.result as u32, before];
        }
    }
}
unsafe fn tick_inner(owner: &mut Owner, link: LinkProgress, now: u32) {
    let Some(p) = (&mut *addr_of_mut!(PENDING)).as_mut() else { return; };
    if p.phase == 6 || p.phase == 9 { return; }
    if p.phase == 3 {
        let faulted = match super::present(super::links().instrument_play_faulted) {
            Some(a) => (core::mem::transmute::<usize, unsafe extern "C" fn(u32) -> i32>(a))(p.ticket.unwrap().generation()) != 0,
            None => false,
        };
        if faulted {
            // Revoke readiness before lease acquisition. Retain published CF code
            // until reboot: UI/service readers may still own its addresses.
            p.was_live = true; p.result = E_DSP_XFER;
            p.phase = if owner.request_link(now, 2000).is_ok() { 8 } else { 9 };
        }
        return;
    }
    if p.phase == 8 {
        if link == LinkProgress::Owned {
            p.phase = if owner.retire(p.ticket.unwrap(), now, 2000).is_ok() { 4 } else { 9 };
        }
        return;
    }
    if p.phase == 0 {
        if owner.request_link(now, 2000).is_err() { p.phase = 9; return; }
        p.phase = 1; return;
    }
    let Some(mut ledger) = crate::package::dsp::instrument_ledger() else { p.phase = 9; return; };
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
                let result = crate::package::loader::publish_v2(p.rec, p.keys as *const u8, p.keys_size,
                    p.init, p.tick, p.image_size, p.selection);
                if result == OK {
                    (*crate::package::resident::state::menus())[p.rec] = p.menu;
                    p.report(OK);
                    crate::package::invariants::check();
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
                if !p.was_live { p.discard(p.result); }
                // Published CF registrations/code remain as a silent tombstone.
                // Only the acknowledged, detached DSP range is reusable here.
                p.phase = if owner.request_link_release(now, 2000).is_ok() { 5 } else { 9 };
            }
            Progress::Pending => {}
            Progress::Blocked(cause) => {
                *super::file() = [9, p.rec as u32, p.result as u32, 100 + cause as u32];
                p.phase = 9;
            }
            _ => { p.phase = 9; }
        }
    } else if p.phase == 5 && link == LinkProgress::Released { p.phase = 6; }
    else if p.phase == 7 && link == LinkProgress::Released { p.phase = 3; }
}
unsafe fn prepare(owner: &mut Owner, p: &mut Pending, ledger: &mut instrument_load::Ledger<'_>, now: u32) -> i32 {
    let Some(policy) = policy() else { return E_CONTRACT; };
    // The DSP section, from the file again: the load checks the file against the hash of its store entry, so it is the file the loader checked (a file
    // replaced since is refused). The staging buffer is free: no load starts while this transaction drains or owns the link (`instruments_idle`), and
    // nothing below keeps a pointer into it past this call (the plan holds the relocated output).
    let Some(file) = p.file else { return E_CONTRACT; };
    if p.section_off.checked_add(p.section_size).map_or(true, |end| end > file.size) { return E_FORMAT; }
    let staged = crate::package::resident::store::load_to_stage(&file);
    if staged != OK { return staged; }
    let bytes = core::slice::from_raw_parts(
        (crate::package::resident::store::stage() as *const u8).add(p.section_off as usize), p.section_size as usize);
    let Some(bounds) = descriptor_limits() else { return E_CONTRACT; };
    let Ok(section) = Section::parse(bytes, crate::package::resident::platform::device(), bounds) else { return E_FORMAT; };
    let Ok(types) = super::resolve_types(&section, p.selection) else { return E_FORMAT; };
    let limits = Limits { l2: Range { start: 0x20000000, end: 0x20080000 },
        reset_frame: 20000, reset_overhead: 1000,
        process_total: policy[3].saturating_mul(policy[2]),
        call_depth: policy[5], stack_bytes: policy[6] };
    let output = core::slice::from_raw_parts_mut(p.output as *mut u8, p.output_size as usize);
    let Ok(plan) = owner.admit(p.rec as u32, &section, types.as_slice(), &limits,
        ledger, output, |_| None) else { return E_DSP_XFER; };
    p.type_no = types.as_slice()[0];
    p.ticket = Some(plan.ticket());
    let Some(budget) = plan.upload_bytes().and_then(upload_budget) else { return E_DSP_XFER; };
    if owner.begin_with_upload_budget(plan, output, now, 2000, budget).is_err() { return E_DSP_XFER; }
    p.phase = 2;
    OK
}

// Preserve the old 64-KiB / 2000-UI-pass allowance per upload page. Include
// zero fill and alignment from the admitted grant, and never exceed the
// executable L2 window. Metadata and recovery retain their separate limits.
fn upload_budget(bytes: u32) -> Option<u32> {
    if bytes == 0 || bytes > 0x80000 { return None; }
    bytes.checked_add(0xffff)?.checked_div(0x10000)?.checked_mul(2000)
}

#[cfg(test)]
mod tests {
    use super::upload_budget;

    #[test]
    fn upload_budget_covers_admitted_image_and_zero_fill_with_a_fixed_ceiling() {
        for (bytes, budget) in [(1, 2000), (65536, 2000), (65537, 4000),
            (207368 + 2240, 8000), (0x80000, 16000)] {
            assert_eq!(upload_budget(bytes), Some(budget));
        }
        for bytes in [0, 0x80001, u32::MAX] {
            assert_eq!(upload_budget(bytes), None);
        }
    }
}
