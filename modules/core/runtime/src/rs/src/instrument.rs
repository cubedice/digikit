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
struct Buffer([u8; dkrt_sdk::instrument_image::CHUNK]);
static mut BUFFER: Buffer = Buffer([0; dkrt_sdk::instrument_image::CHUNK]);

/// Emulator-only bootstrap diagnostic: phase (idle/acquire/bind/release/bound/
/// blocked), token, start tick. No retry, admission, or capability publication.
/// The optional trigger exists only in test/instrument-session images.
#[no_mangle]
pub static mut DKRT_INSTRUMENT_SESSION: [u32; 3] = [0; 3];

/// CAPACITY of persistent instruments (`CAPK_INSTRUMENT_ROWS`, `CAPK_INSTRUMENT_TRACKS`): (rows limit, rows used, rows a file can still
/// take) and (tracks limit, tracks used). The limits are the registry's rows and the tracks the firmware adapter's policy admits; zeros
/// without the adapter. A boot takes one file of instruments (`pending::occupied`): after it, no file takes a row.
pub unsafe fn capacity() -> ((u32, u32, u32), (u32, u32)) {
    let Some(policy) = pending::policy() else {
        return ((0, 0, 0), (0, 0));
    };
    let rows_limit = dkrt_sdk::instrument_registry::MAX_ROWS as u32;
    let (mut rows_used, mut tracks_used) = (0, 0);
    if let Some(owner) = (&*core::ptr::addr_of!(OWNER)).as_ref() {
        let c = owner.loads().capacity();
        rows_used = c.rows_used;
        tracks_used = c.tracks_used;
    }
    let open = if pending::occupied() { 0 } else { rows_limit.saturating_sub(rows_used) };
    ((rows_limit, rows_used, open), (policy[2], tracks_used))
}

/// The type numbers of the records of `section`, for the module at index `selection` of the boot's selection. The lookup uses the saved selection
/// index, not the module being loaded now. The keys and their numbers are a copy of the plan of the service that numbers them (`Service::planned_ids`);
/// a number is refused when a service has it taken in its rows (`Service::id_taken`) or a legacy DSP row holds it, published or not. UI task only.
pub unsafe fn resolve_types(
    section: &dkrt_sdk::instrument_section::Section<'_>,
    selection: u32,
) -> Result<dkrt_sdk::instrument_types::Resolved, dkrt_sdk::instrument_types::Error> {
    use dkrt_sdk::device::{DYN_TYPE_FIRST, DYN_TYPE_LAST};
    if selection >= dkrt_sdk::SEL_MAX as u32 {
        return Err(dkrt_sdk::instrument_types::Error::Map);
    }
    let mut pairs = [[0u32; 2]; 8];
    let n = crate::handlers::exports::planned_ids(selection, &mut pairs);
    let (mut keys, mut types) = ([0u32; 8], [0u32; 8]);
    let mut i = 0;
    while i < n {
        keys[i] = pairs[i][0];
        types[i] = pairs[i][1];
        i += 1;
    }
    let context = dkrt_sdk::instrument_types::Context {
        selection,
        indices: &[selection; 8][..n],
        keys: &keys[..n],
        types: &types[..n],
        first: DYN_TYPE_FIRST,
        last: DYN_TYPE_LAST,
    };
    section.resolve_types(&context, |t| crate::handlers::exports::id_taken(t) || !crate::dsp::type_is_free(t))
}

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
            && !crate::link::lk().in_flight();
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
            Loads::new(match pending::policy() {
                Some(policy) => policy[2],
                None if probe::present() => 1,
                None => 0,
            }),
        ));
    }
}
