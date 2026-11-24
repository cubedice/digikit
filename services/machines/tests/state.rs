//! The owner `Machines` itself: what `start` and `start_in` do (the image table is bound into `DKRT_MF_TAB`, or the file is refused; the optional table of the SRC pages
//! into `DKRT_MF_EXT`; the state is made where it lives, over whatever the slot held), and what a new owner holds (nothing collected, the gate closed, no action armed,
//! nothing parked).
use core::mem::MaybeUninit;
use dkneeds::keys::name_hash;
use dkrt_abi::provider::Provider;
use dkrt_sdk::E_NO_REGISTRY;
use dkrt_svc_machines::ext_info::{self, DKRT_MF_EXT};
use dkrt_svc_machines::info::{self, w, DKRT_MF_TAB};
use dkrt_svc_machines::machines::Machines;
use dkrt_test_support::FakeCore;
use std::sync::Mutex;

/// `DKRT_MF_TAB` and `DKRT_MF_EXT` are tables of the crate: the tests that bind them take turns.
static TURN: Mutex<()> = Mutex::new(());

fn table(device: u32) -> Vec<u32> {
    let mut t = vec![0u32; info::WORDS];
    t[w::MAGIC] = info::MAGIC;
    t[w::VERSION] = info::VERSION;
    t[w::DEVICE] = device;
    t[w::ENSURE] = 0x4000_1000;
    t[w::ROWS] = 0x4200_0000;
    t[w::FIRST] = 7;
    t[w::N_CODE] = 0x4000_2000;
    t
}

fn core_with(words: Option<Vec<u32>>, device: u32) -> FakeCore {
    let mut c = FakeCore::new();
    c.device = device;
    if let Some(t) = words {
        c.info_tables.push((name_hash("machines_info"), t));
    }
    c
}

/// What a fresh owner shows from outside.
fn assert_initial(m: &mut Machines) {
    let ph = m.builder().placeholder();
    assert!(!ph.is_null());
    assert_eq!(m.builder().build().nm, 0);
    assert!(m.builder().args().is_none());
    assert_eq!(m.clicks().pending().callback, 0);
    let p = m.parking();
    assert_eq!(
        (p.missing_rows(), p.tries(), p.logged(), p.busy_passes()),
        (0, 0, 0, 0)
    );
    assert!(m.applied().codes().is_empty());
    assert_eq!(m.applied().native_seen(), 0);
}

#[test]
fn a_new_owner_has_nothing_collected_queued_or_parked() {
    let mut m = Box::new(Machines::new());
    assert_initial(&mut m);
}

#[test]
fn start_refuses_an_image_without_a_usable_table_and_binds_a_good_one() {
    let _t = TURN.lock().unwrap_or_else(|e| e.into_inner());
    // no table, another device's table, no `ENSURE` word
    let mut no_ensure = table(2);
    no_ensure[w::ENSURE] = 0;
    for core in [
        core_with(None, 2),
        core_with(Some(table(1)), 2),
        core_with(Some(no_ensure), 2),
    ] {
        assert!(matches!(
            <Machines as Provider<FakeCore>>::start(&core),
            Err(E_NO_REGISTRY)
        ));
        let mut slot = MaybeUninit::<Machines>::uninit();
        assert_eq!(
            <Machines as Provider<FakeCore>>::start_in(&core, &mut slot),
            Err(E_NO_REGISTRY)
        );
    }
    let core = core_with(Some(table(2)), 2);
    let mut owner = <Machines as Provider<FakeCore>>::start(&core).expect("starts");
    assert_initial(&mut owner);
    // SAFETY: the test holds the turn; nothing else reads the table
    let bound = unsafe { *core::ptr::addr_of!(DKRT_MF_TAB) };
    assert_eq!(bound[..], table(2)[..], "the hooks' table is the image's");
}

/// The image table of the SRC pages and modulation routes (`machines_ext_info`, with `instrument_mod_param` bound when `modulation`).
fn ext_table(modulation: bool) -> Vec<u32> {
    let mut t = vec![0u32; ext_info::WORDS];
    t[ext_info::w::MAGIC] = ext_info::MAGIC;
    t[ext_info::w::VERSION] = ext_info::VERSION;
    t[ext_info::w::SRC_COUNT] = 0x4000_3000;
    t[ext_info::w::SRC_APPLY] = 0x4000_3100;
    if modulation {
        t[ext_info::w::MOD_PARAM] = 0x4000_3200;
    }
    t
}

/// The table the hooks read for the SRC pages, as the last `start` left it.
fn bound_ext() -> [u32; ext_info::WORDS] {
    // SAFETY: the test holds the turn; nothing else writes the table
    unsafe { *core::ptr::addr_of!(DKRT_MF_EXT) }
}

#[test]
fn start_binds_the_src_page_table_when_the_image_has_one_and_clears_it_when_not() {
    let _t = TURN.lock().unwrap_or_else(|e| e.into_inner());
    for start_in in [false, true] {
        let start = |core: &FakeCore| {
            if start_in {
                let mut slot = Box::new(MaybeUninit::<Machines>::uninit());
                <Machines as Provider<FakeCore>>::start_in(core, &mut slot)
            } else {
                <Machines as Provider<FakeCore>>::start(core).map(|_| ())
            }
        };
        let mut core = core_with(Some(table(1)), 1);
        core.info_tables
            .push((name_hash("machines_ext_info"), ext_table(true)));
        assert_eq!(start(&core), Ok(()));
        assert_eq!(bound_ext()[..], ext_table(true)[..], "start_in {start_in}");
        // a later start over an image without the table (or with one that is not usable) leaves nothing of the earlier one; the file still starts
        let mut unusable = ext_table(true);
        unusable[ext_info::w::SRC_APPLY] = 0;
        for ext in [None, Some(unusable)] {
            let mut core = core_with(Some(table(1)), 1);
            if let Some(t) = ext {
                core.info_tables.push((name_hash("machines_ext_info"), t));
            }
            assert_eq!(start(&core), Ok(()));
            assert_eq!(bound_ext(), [0; ext_info::WORDS], "start_in {start_in}");
        }
        // without core/instrument-play the modulation word is 0, and the SRC pages are still bound
        let mut core = core_with(Some(table(1)), 1);
        core.info_tables
            .push((name_hash("machines_ext_info"), ext_table(false)));
        assert_eq!(start(&core), Ok(()));
        assert_eq!(bound_ext()[..], ext_table(false)[..]);
        // a refused file binds neither table
        let mut core = core_with(None, 1);
        core.info_tables
            .push((name_hash("machines_ext_info"), ext_table(true)));
        assert_eq!(start(&core), Err(E_NO_REGISTRY));
        assert_eq!(
            bound_ext()[..],
            ext_table(false)[..],
            "untouched by a refused start"
        );
    }
}

#[test]
fn start_in_makes_the_state_where_it_lives_over_whatever_the_slot_held() {
    let _t = TURN.lock().unwrap_or_else(|e| e.into_inner());
    let core = core_with(Some(table(2)), 2);
    let mut slot = Box::new(MaybeUninit::<Machines>::uninit());
    // a slot that held a former owner's bytes
    // SAFETY: the slot is plain memory of the size of `Machines`
    unsafe { slot.as_mut_ptr().write_bytes(0xa5, 1) };
    assert_eq!(
        <Machines as Provider<FakeCore>>::start_in(&core, &mut slot),
        Ok(())
    );
    // SAFETY: `start_in` made the state in place
    let m = unsafe { slot.assume_init_mut() };
    assert_initial(m);
}
