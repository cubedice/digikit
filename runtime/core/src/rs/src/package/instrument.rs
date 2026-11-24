//! The fork's persistent V2 instrument host on the UI task: the boot-owned V2 controller and the legacy traffic arbitration. It is the system package's,
//! beside the DSP loader and the link queue it watches; the package's LAST tick phase runs it (`hooks.rs`). The image functions it binds come from the resident
//! link rows (`dkrt_abi::resident::InstrumentLinks`), the statics the image adapters and the tests read are resident and written through the views
//! (`InstrumentViews`, the resident `instrument.rs`). Normal-file admission requires an explicit trusted firmware policy.
use core::ptr::addr_of_mut;
use dkrt_abi::platform::DeviceProfile;
use dkrt_abi::resident::{InstrumentLinks, InstrumentViews};
use dkrt_sdk::{
    instrument_coordinator::{Coordinator, LegacyPolicy, LinkProgress, Progress},
    instrument_lease::OwnedApi,
    instrument_load::Loads,
    instrument_provider::ExpectSession,
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

/// The image functions of the host (0 when their module is not in the image).
#[inline(always)]
pub(crate) fn links() -> InstrumentLinks {
    crate::package::resident::links::instrument()
}

#[inline(always)]
fn views() -> InstrumentViews {
    crate::package::resident::state::instrument()
}

/// `DKRT_INSTRUMENT_FILE`: the diagnostics of a normal-file V2 load (phase, record, result, last progress or failure).
pub(crate) unsafe fn file() -> &'static mut [u32; 4] {
    &mut *views().file
}

/// `DKRT_INSTRUMENT_SESSION`: the emulator-only bootstrap diagnostic (phase, token, start tick).
unsafe fn session() -> &'static mut [u32; 3] {
    &mut *views().session
}

/// `DKRT_INSTRUMENT_LOAD`: the status words of the emulator-only lifecycle probe.
pub(crate) unsafe fn load_status() -> &'static mut [u32; 6] {
    &mut *views().load
}

/// An address of the link rows as a function, or `None` (the black box keeps the test of 0: Rust takes a function's address to be non-zero).
#[inline(always)]
pub(crate) fn present(f: *const ()) -> Option<usize> {
    let a = core::hint::black_box(f as usize);
    (a != 0).then_some(a)
}

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

/// The machines file's export `dk_machines_planned_ids(selection, out)`: the `[key, number]` pairs it planned for the module at index `selection`; how many
/// (none without the file).
unsafe fn planned_ids(selection: u32, out: &mut [[u32; 2]; 8]) -> usize {
    match crate::package::loader::export_addr(dkneeds::keys::name_hash("dk_machines_planned_ids")) {
        Some(a) => {
            let f: unsafe extern "C" fn(u32, *mut u32) -> u32 = core::mem::transmute(a as usize);
            (f(selection, out.as_mut_ptr() as *mut u32) as usize).min(out.len())
        }
        None => 0,
    }
}

/// The machines file's export `dk_machines_id_taken(id)`: whether it has machine type `id` taken in its rows (not taken without the file).
unsafe fn id_taken(id: u32) -> bool {
    match crate::package::loader::export_addr(dkneeds::keys::name_hash("dk_machines_id_taken")) {
        Some(a) => {
            let f: unsafe extern "C" fn(u32) -> u32 = core::mem::transmute(a as usize);
            f(id) != 0
        }
        None => false,
    }
}

/// The type numbers of the records of `section`, for the module at index `selection` of the boot's selection. The lookup uses the saved selection
/// index, not the module being loaded now. The keys and their numbers are a copy of the plan of the service that numbers them (the machines service's export `dk_machines_planned_ids`);
/// a number is refused when a service has it taken in its rows (its export `dk_machines_id_taken`) or a legacy DSP row holds it, published or not. UI task only.
pub unsafe fn resolve_types(
    section: &dkrt_sdk::instrument_section::Section<'_>,
    selection: u32,
) -> Result<dkrt_sdk::instrument_types::Resolved, dkrt_sdk::instrument_types::Error> {
    // the run-time machine types of this device (its profile)
    let profile = DeviceProfile::by_id(crate::package::resident::platform::device());
    let first = profile.map_or(0, |p| p.dyn_type_first);
    let count = profile.map_or(0, |p| p.dyn_types);
    if selection >= dkrt_sdk::SEL_MAX as u32 {
        return Err(dkrt_sdk::instrument_types::Error::Map);
    }
    let mut pairs = [[0u32; 2]; 8];
    let n = planned_ids(selection, &mut pairs);
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
        first,
        last: first + count.saturating_sub(1),
    };
    section.resolve_types(&context, |t| id_taken(t) || !crate::package::dsp::type_is_free(t))
}

/// UI-task only. Without all providers, legacy scheduling is unchanged.
pub unsafe fn legacy_policy() -> LegacyPolicy {
    match (&*core::ptr::addr_of!(OWNER)).as_ref() {
        Some(owner) => owner.legacy_policy(),
        None => LegacyPolicy::Run,
    }
}

/// The package's LAST tick phase, after the services' ticks (the legacy load and FX ticks, so their final poll is consumed before ownership changes):
/// bind once, run the coordinator, publish the type and generation the image adapters read. Physical END drainage is separately checked by dl_claim.
pub unsafe fn ui_pass(now: u32) {
    bind_once();
    tick(now);
    let (t, g) = pending::published();
    let w = &mut *views().words;
    core::ptr::write_volatile(&mut w[0], t);
    core::ptr::write_volatile(&mut w[1], g);
}

unsafe fn tick(now: u32) {
    if let Some(owner) = (&mut *addr_of_mut!(OWNER)).as_mut() {
        let d = crate::package::dsp::ds();
        let idle = d.phase == crate::package::dsp::PH_IDLE && d.nwait == 0 && !crate::package::link::lk().in_flight();
        let link = owner.tick_link(now, idle);
        let proof = session();
        if let LinkProgress::Blocked(cause) = link {
            proof[0] = 5;
            pending::blocked(100 + cause as u32);
            return;
        }
        // A dense match emits an inline jump table on this ColdFire backend.
        if proof[0] == 0 {
            let probe = present(links().instrument_session_probe);
            if probe.is_some() || pending::occupied() {
                // Module set is boot-only. A fresh CF/DSP boot owns this session;
                // any observed DSP-only reset remains a blocked session.
                let token = match probe {
                    _ if pending::occupied() => 1,
                    Some(a) => {
                        let f: unsafe extern "C" fn() -> u32 = core::mem::transmute(a);
                        f()
                    }
                    None => 0,
                };
                if token != 0 {
                    proof[1] = token;
                    proof[2] = now;
                    proof[0] = if owner.request_link(now, 2000).is_ok() { 1 } else { 5 };
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
                    proof[0] = if owner.request_link_release(now, 2000).is_ok() { 3 } else { 5 };
                }
                Progress::Blocked(_) => proof[0] = 5,
                _ => {}
            }
        } else if proof[0] == 3 && link == LinkProgress::Released {
            proof[0] = 4;
        }
        if proof[0] == 5 {
            pending::blocked(200);
        }
        if proof[0] == 4 {
            if pending::occupied() {
                pending::tick(owner, link, now);
            } else {
                probe::tick(owner, link, now);
            }
        }
    }
}

/// Bind the providers once (`DKRT_INSTRUMENT_BINDING`, whose header is the read-only diagnostic: attempted, symbol-presence mask, bound). Never reset for a
/// DSP-only fault; only a whole ColdFire boot creates a fresh namespace.
unsafe fn bind_once() {
    let binding = &mut *views().binding;
    if binding.diagnostics()[0] != 0 {
        return;
    }
    let l = links();
    let a = |f: *const ()| core::hint::black_box(f as usize);
    binding.bind_once([
        a(l.dl_claim),
        a(l.dl_release),
        a(l.dl_send_owned),
        a(l.dl_poll_tagged),
        a(l.mi_registry_reply),
        a(l.mi_registry_expect_session),
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
