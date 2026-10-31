//! Boot-owned V2 controller and legacy traffic arbitration on the UI task.
//! No admission or capabilities are enabled until the loader is integrated.
use core::ptr::addr_of_mut;
use dkrt_sdk::{
    instrument_coordinator::{Coordinator, LegacyPolicy},
    instrument_lease::OwnedApi,
    instrument_load::Loads,
    instrument_provider::Binding,
    instrument_transport::Driver,
};

type Owner = Coordinator<'static, OwnedApi>;
static mut OWNER: Option<Owner> = None;
#[repr(C, align(16))]
struct Buffer([u8; 256]);
static mut BUFFER: Buffer = Buffer([0; 256]);

/// UI-task only. Without all providers, legacy scheduling is unchanged.
pub unsafe fn legacy_policy() -> LegacyPolicy {
    match (&*core::ptr::addr_of!(OWNER)).as_ref() {
        Some(owner) => owner.legacy_policy(),
        None => LegacyPolicy::Run,
    }
}

/// Run after legacy load and FX ticks so their final poll is consumed before
/// ownership changes. Physical END drainage is separately checked by dl_claim.
pub unsafe fn tick(now: u32) {
    if let Some(owner) = (&mut *addr_of_mut!(OWNER)).as_mut() {
        let d = crate::dsp::ds();
        let idle = d.phase == crate::dsp::PH_IDLE
            && d.nwait == 0
            && !crate::fxpage::busy()
            && !crate::fxsend::busy();
        let _ = owner.tick_link(now, idle);
    }
}

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
    if let Some(providers) = binding.take() {
        let (transport, replies) = providers.into_parts();
        let buffer = &mut (*addr_of_mut!(BUFFER)).0;
        // Zero admitted instances until device budgets and the shared allocator
        // are connected. Provider presence must never activate a machine.
        OWNER = Some(Coordinator::new(
            Driver::new(transport, buffer),
            replies,
            Loads::new(0),
        ));
    }
}
