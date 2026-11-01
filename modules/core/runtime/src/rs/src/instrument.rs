//! Boot-owned V2 controller and legacy traffic arbitration on the UI task.
//! Normal-file admission requires an explicit trusted firmware policy.
use core::ptr::addr_of_mut;
use dkrt_sdk::{
    instrument_coordinator::{Coordinator, LegacyPolicy, LinkProgress, Progress},
    instrument_lease::OwnedApi,
    instrument_load::Loads,
    instrument_provider::{Binding, ExpectSession},
    instrument_transport::Driver,
};

type Owner = Coordinator<'static, OwnedApi>;
mod probe;
pub(crate) mod pending;
static mut OWNER: Option<Owner> = None;
// Bound atomically with the snapshot/transport set. A queued V2 file or the
// optional emulator probe starts the one-shot bootstrap.
#[used]
static mut EXPECT_SESSION: Option<ExpectSession> = None;
#[repr(C, align(16))]
struct Buffer([u8; 256]);
static mut BUFFER: Buffer = Buffer([0; 256]);

/// Emulator-only bootstrap diagnostic: phase (idle/acquire/bind/release/bound/
/// blocked), token, start tick. No retry, admission, or capability publication.
/// The optional trigger exists only in test/instrument-session images.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_SESSION: [u32; 3] = [0; 3];

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
        let link = owner.tick_link(now, idle);
        let proof = &mut *addr_of_mut!(DKRT_INSTRUMENT_SESSION);
        if let LinkProgress::Blocked(cause) = link {
            proof[0] = 5;
            pending::blocked(100 + cause as u32);
            return;
        }
        // A dense match emits an inline jump table on this ColdFire backend.
        if proof[0] == 0 {
            let probe = core::hint::black_box(instrument_session_probe as usize);
            if probe != 0 || pending::occupied() {
                // Module set is boot-only. A fresh CF/DSP boot owns this session;
                // any observed DSP-only reset remains a blocked session.
                let token = if pending::occupied() { 1 } else { instrument_session_probe() };
                if token != 0 {
                    proof[1] = token;
                    proof[2] = now;
                    proof[0] = if owner.request_link(now, 2000).is_ok() {
                        1
                    } else {
                        5
                    };
                }
            }
        } else if proof[0] == 1 {
            if link == LinkProgress::Owned {
                proof[0] = match EXPECT_SESSION {
                    Some(expect) if owner.begin_session(expect, proof[1], now, 2000).is_ok() => 2,
                    _ => 5,
                };
            }
        } else if proof[0] == 2 {
            match owner.tick_bootstrap(now) {
                Progress::Idle if owner.session_bound() => {
                    proof[0] = if owner.request_link_release(now, 2000).is_ok() {
                        3
                    } else {
                        5
                    };
                }
                Progress::Blocked(_) => proof[0] = 5,
                _ => {}
            }
        } else if proof[0] == 3 && link == LinkProgress::Released {
            proof[0] = 4;
        }
        if proof[0] == 5 { pending::blocked(200); }
        if proof[0] == 4 {
            if pending::occupied() { pending::tick(owner, link, now); }
            else { probe::tick(owner, link, now); }
        }
    }
}

extern "C" {
    // Optional patchkit relocations resolve to zero when their module is absent.
    fn instrument_session_probe() -> u32;
    fn dl_claim();
    fn dl_release();
    fn dl_send_owned();
    fn dl_poll_tagged();
    fn mi_registry_reply();
    fn mi_registry_expect_session();
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
        core::hint::black_box(mi_registry_expect_session as usize),
    ]);
    if let Some((providers, expect)) = binding.take() {
        EXPECT_SESSION = Some(expect);
        let (transport, replies) = providers.into_parts();
        let buffer = &mut (*addr_of_mut!(BUFFER)).0;
        // Zero admitted instances until device budgets and the shared allocator
        // are connected. Provider presence must never activate a machine.
        OWNER = Some(Coordinator::new(
            Driver::new(transport, buffer),
            replies,
            Loads::new(if probe::present() || pending::policy().is_some() { 1 } else { 0 }),
        ));
    }
}
