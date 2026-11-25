//! Retained normal-file V2 load, serialized on the owning UI task.
//! A pending CF record is occupied but unpublished; no staging-buffer pointer
//! survives boot. Uncertain DSP failures retain the entire record until reboot.
use super::{Owner, LinkProgress, Progress};
use core::ptr::addr_of_mut;
use dkrt_abi::storage::StoreFile;
use dkrt_sdk::{*, instrument_admission::{Limits, Range}, instrument_descriptor, instrument_image::CHUNK,
    instrument_load::Command, instrument_registry::Ticket, instrument_section::Section,
    instrument_stream::{Reader, Stream}};

/// The output buffer of a V2 load (in its arena block, 16-byte aligned): the chunk of the relocated image being sent, then the two sector
/// buffers of `CardReader`.
pub(crate) const UPLOAD_BUFFER: u32 = dkneeds::instrument::UPLOAD_BUFFER;
const _: () = assert!(UPLOAD_BUFFER as usize == CHUNK + 2 * 512);

pub(crate) struct Pending {
    pub rec: usize,
    pub selection: u32,
    pub image_size: u32,
    /// The copy of the file's keys table (`complete` reads the machine keys from it).
    pub keys: u32,
    pub keys_size: u32,
    /// The store file the loader staged for this load, and where its DSP section is in it: `prepare` stages the file again and admits the section
    /// there, and the upload reads the image from the file once more (the arena block holds no copy of it).
    pub file: Option<StoreFile>,
    pub section_off: u32,
    pub section_size: u32,
    /// The output buffer (`UPLOAD_BUFFER` bytes).
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
        file: Option<StoreFile>, section_off: u32, section_size: u32, output: u32, output_size: u32,
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
    // nothing below keeps a pointer into it past this call: the upload reads the image from the file again (`STREAM`), and checks it is the one
    // admitted here.
    let Some(file) = p.file else { return E_CONTRACT; };
    if p.section_off.checked_add(p.section_size).map_or(true, |end| end > file.size) { return E_FORMAT; }
    if p.output % 16 != 0 || p.output_size < UPLOAD_BUFFER { return E_CONTRACT; }
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
    let Ok((plan, relocation)) = owner.admit_streamed(p.rec as u32, &section, types.as_slice(), &limits,
        ledger, |_| None) else { return E_DSP_XFER; };
    p.type_no = types.as_slice()[0];
    let ticket = plan.ticket();
    p.ticket = Some(ticket);
    let Some(budget) = plan.upload_bytes().and_then(upload_budget) else { return E_DSP_XFER; };
    let reader = CardReader { file, section_off: p.section_off, section_size: p.section_size,
        sectors: (p.output as *mut u8).add(CHUNK), held: [u32::MAX; 2], last: 0 };
    // No job uses the stream: `admit_streamed` refuses while one runs.
    let stream = &mut *addr_of_mut!(STREAM);
    let image_len = stream.insert(Stream::new(&section, relocation, reader)).image_len();
    let source = stream.as_mut().unwrap();
    let chunk = &mut *(p.output as *mut [u8; CHUNK]);
    if owner.begin_streamed_with_upload_budget(plan, source, image_len, chunk, now, 2000, budget).is_err() {
        // nothing was sent (a first chunk that could not be read, or a busy owner): give the admission back when the owner takes it
        if owner.cancel_admission(ticket, ledger).is_ok() { p.ticket = None; }
        return E_DSP_XFER;
    }
    p.phase = 2;
    OK
}

/// The stream of the load being sent (one at a time); the owner's job borrows it until the job ends.
static mut STREAM: Option<Stream<CardReader>> = None;

/// The DSP section's bytes from its store file, through two sector buffers (the upload reads the image and the relocation table in turns). A
/// file sector is read with the store's own sector read, which takes the store lock for that one read: nothing is held between UI passes.
struct CardReader {
    file: StoreFile,
    section_off: u32,
    section_size: u32,
    /// Two 512-byte buffers (16-byte aligned, in the output buffer after the chunk), the file sector each holds (`u32::MAX`: none), and the one
    /// used last.
    sectors: *mut u8,
    held: [u32; 2],
    last: usize,
}

impl CardReader {
    /// The buffer that holds file sector `sector`, read now when neither does (into the one used less recently).
    fn buffer(&mut self, sector: u32) -> Result<*const u8, ()> {
        let i = if self.held[0] == sector { 0 } else if self.held[1] == sector { 1 } else {
            let i = 1 - self.last;
            self.held[i] = u32::MAX;
            let at = unsafe { self.sectors.add(i * 512) };
            if unsafe { crate::package::resident::store::read_sector(self.file.first + sector, at) } != OK { return Err(()); }
            self.held[i] = sector;
            i
        };
        self.last = i;
        Ok(unsafe { self.sectors.add(i * 512) })
    }
}

impl Reader for CardReader {
    fn read(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()> {
        if offset.checked_add(out.len() as u32).map_or(true, |end| end > self.section_size) { return Err(()); }
        let mut at = self.section_off + offset;
        let mut done = 0;
        while done < out.len() {
            let within = (at % 512) as usize;
            let n = (512 - within).min(out.len() - done);
            let buf = self.buffer(at / 512)?;
            unsafe { core::ptr::copy_nonoverlapping(buf.add(within), out.as_mut_ptr().add(done), n); }
            done += n;
            at += n as u32;
        }
        Ok(())
    }
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
