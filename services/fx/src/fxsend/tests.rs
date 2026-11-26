use super::*;
use dkrt_abi::dsp;
use dkrt_sdk::*;

/// The offsets that the device tests read from `DKRT_FXS` (`fx-track-route.device.test.ts`).
#[test]
fn the_layout_the_device_tests_read_is_unchanged() {
    assert_eq!(core::mem::offset_of!(SendState, restores), 16);
    assert_eq!(core::mem::offset_of!(SendState, allow), 40);
    assert_eq!(core::mem::offset_of!(SendState, dflt), 44);
}

/// The four DSP amount words take the 16 track amounts as bytes: track t is byte t % 4 of word t / 4 (low byte first).
#[test]
fn send_amounts_are_packed_four_tracks_to_a_word() {
    let mut a = [0u8; TRACKS];
    a[0] = 1;
    a[1] = 2;
    a[3] = 127;
    a[4] = 5;
    a[15] = 9;
    assert_eq!(words_of(&a), [0x7f00_0201, 5, 0, 9 << 24]);
    assert_eq!(words_of(&[0; TRACKS]), [0; 4]);
}

/// The routes a record allows and its default, from its flags: master unless NOMASTER, send when SEND, default send only with DEFAULT_SEND.
#[test]
fn route_flags_give_the_allowed_routes_and_the_default() {
    assert_eq!(routes_of(0), (ROUTE_MASTER, ROUTE_MASTER));
    assert_eq!(
        routes_of(FXF_SEND),
        (ROUTE_MASTER | ROUTE_SEND, ROUTE_MASTER)
    );
    assert_eq!(
        routes_of(FXF_NOMASTER | FXF_SEND),
        (ROUTE_SEND, ROUTE_MASTER)
    );
    assert_eq!(
        routes_of(FXF_SEND | FXF_DEFAULT_SEND),
        (ROUTE_MASTER | ROUTE_SEND, ROUTE_SEND)
    );
}

/// The settings keys are `fx/route/<module id>` and `fx/send/<module id>`; the id is copied up to its length.
#[test]
fn settings_keys_carry_the_module_id() {
    let mut id = [0u8; 32];
    id[..9].copy_from_slice(b"fx/gain-x");
    let mut buf = [0u8; KEY_MAX];
    let n = key_of(&mut buf, b"fx/send/", &id, 9);
    assert_eq!(&buf[..n], b"fx/send/fx/gain-x");
}

/// The saved route is used when the effect allows it; a route it does not allow, a value that is no route and no entry give the default.
#[test]
fn the_saved_route_falls_back_to_the_default() {
    use dkrt_test_support::FakeCore;
    let _g = crate::test_lock::hold();
    let core = FakeCore::new();
    unsafe {
        let s = ss();
        s.id[3][..4].copy_from_slice(b"mod0");
        s.idlen[3] = 4;
        s.allow[3] = ROUTE_MASTER | ROUTE_SEND;
        s.dflt[3] = ROUTE_MASTER;
        assert_eq!(saved_route(&core, 3), ROUTE_MASTER, "no entry: the default");
        core.settings
            .borrow_mut()
            .push((b"fx/route/mod0".to_vec(), std::vec![1]));
        assert_eq!(saved_route(&core, 3), ROUTE_SEND);
        s.allow[3] = ROUTE_MASTER;
        assert_eq!(
            saved_route(&core, 3),
            ROUTE_MASTER,
            "the effect no longer allows send"
        );
        s.allow[3] = ROUTE_MASTER | ROUTE_SEND;
        core.settings.borrow_mut()[0].1 = std::vec![7];
        assert_eq!(saved_route(&core, 3), ROUTE_MASTER, "7 is no route");
        // an effect that was a master effect and is a send-only effect now (PRISM): a stored master route is not used
        core.settings.borrow_mut()[0].1 = std::vec![0];
        s.allow[3] = ROUTE_SEND;
        s.dflt[3] = ROUTE_SEND;
        assert_eq!(saved_route(&core, 3), ROUTE_SEND);
    }
}

/// The DSP route word: a cell of the send page is the send bus; without one the master, or neither route for a send-only effect.
#[test]
fn an_effect_without_a_cell_runs_on_the_master_only_if_it_may() {
    assert_eq!(route_word(ROUTE_MASTER | ROUTE_SEND, true), 1);
    assert_eq!(route_word(ROUTE_SEND, true), 1);
    assert_eq!(route_word(ROUTE_MASTER | ROUTE_SEND, false), 0);
    assert_eq!(route_word(ROUTE_MASTER, false), 0);
    assert_eq!(
        route_word(ROUTE_SEND, false),
        2,
        "send only and no cell: not run"
    );
    // the same at the claim, before the settings are read
    assert_eq!(crate::dsp::claim_route(0), 0);
    assert_eq!(crate::dsp::claim_route(FXF_SEND | FXF_DEFAULT_SEND), 0);
    assert_eq!(
        crate::dsp::claim_route(FXF_SEND | FXF_NOMASTER | FXF_DEFAULT_SEND),
        2
    );
}

/// An image whose `rows_info` holds the parameter table and the blocks of the effect knobs (owner 2, from code 40) and the send knobs (owner 3, from code 90), with the
/// effect pages of a small device of `cells` send cells bound (the facts `tests/table.rs` uses), and the effect chain of `core/dynfx`.
fn send_page_core(cells: u32) -> dkrt_test_support::FakeCore {
    use dkrt_svc_rows::rows_info::w as r;
    let mut core = dkrt_test_support::FakeCore::new();
    let mut d = std::vec![0u32; 110];
    d[r::P_TABLE] = 0x10000;
    d[r::PF_SIZE] = 0x40;
    d[r::P_ROWS] = 0x20000;
    d[r::P_ROW_SIZE] = 0x20;
    d[r::P_TEMPLATE_OFF] = 8;
    core.set_rows_info(d);
    core.set_rows_blocks(&[(1, 20, 16, 0xd1), (2, 40, 24, 0x75), (3, 90, 5, 0x75)]);
    core.set_dynfx(0, 4);
    let facts = crate::fxpage::DeviceFacts {
        device: core.device,
        page_first: 14,
        slots: 3,
        array_off: 0x14,
        values_n: 32,
        mark_index: 70,
        send_cells: cells,
        knob_block: 2,
        send_block: 3,
        track_page: 13,
        store_off: 0x80,
        ..Default::default()
    };
    // SAFETY: the bound table is this crate's static; the tests that bind it hold the test lock
    assert_eq!(
        unsafe { crate::fxpage::bind_facts(&core, &facts, [0, 0]) },
        OK
    );
    core
}

/// The tables these tests change back to the start: the rows (and the phrase claim), the send state and the bound table.
unsafe fn clean() {
    crate::rows::reset();
    core::ptr::write_bytes(core::ptr::addr_of_mut!(DKRT_FXS), 0, 1);
    crate::fxpage::unbind();
}

/// What the phrase adapters give in these tests (`phrase_sends`): the amounts of `PHRASE_SENDS`, or nothing while it is `None`.
static PHRASE_SENDS: std::sync::Mutex<Option<[u8; TRACKS]>> = std::sync::Mutex::new(None);
unsafe extern "C" fn phrase_sends(out: *mut u8) -> u32 {
    match *PHRASE_SENDS.lock().unwrap_or_else(|e| e.into_inner()) {
        Some(a) => {
            core::ptr::copy_nonoverlapping(a.as_ptr(), out, TRACKS);
            1
        }
        None => 0,
    }
}
fn give(a: Option<[u8; TRACKS]>) {
    *PHRASE_SENDS.lock().unwrap_or_else(|e| e.into_inner()) = a;
}

/// The phrase effect (PRISM) runs on a send bus without a cell of the send page: its amounts are what the phrase adapters give, before and after a
/// selection changes, and none are kept in `fx/send/<id>`. A send effect that loads after it takes the first cell, as it did before the phrase effect
/// gave its cell up, and its amounts are the stored ones. Without `phrase_sends` in the image the phrase effect is a send effect like any other.
#[test]
fn the_phrase_effect_takes_its_amounts_from_its_adapters_and_no_cell() {
    use dkrt_test_support::Call;
    let _g = crate::test_lock::hold();
    let send_only = FXF_SEND | FXF_NOMASTER | FXF_DEFAULT_SEND;
    unsafe {
        clean();
        let core = send_page_core(2);
        let d = crate::rows::frows();
        for (k, owner) in [(0usize, 1u8), (1, 2)] {
            d.fx_owner[k] = owner;
            d.fx_pub[k] = 1;
            d.fx_flags[k] = send_only;
        }
        // the other send effect's stored amounts: track 2 at 50
        let mut stored = [0u8; TRACKS];
        stored[1] = 50;
        core.settings
            .borrow_mut()
            .push((b"fx/send/fake".to_vec(), stored.to_vec()));
        crate::phrase::reserve(1, 0);
        crate::phrase::set_sends(phrase_sends as *const () as usize);
        // the adapters have not bound the effect yet: no amounts
        give(None);
        apply(&core, 0, 1, b"PRS\0".as_ptr());
        let s = ss();
        assert_eq!(
            (s.route[0], s.cell[0], used()),
            (1, 0, 0),
            "a send bus, no cell, no send slot used"
        );
        assert_eq!((d.fx_route[0], d.fx_amt[0]), (1, [0; 4]));
        // the selection of the page: tracks 3 and 11
        let mut a = [0u8; TRACKS];
        a[2] = 127;
        a[10] = 127;
        give(Some(a));
        tick(&core);
        assert_eq!(s.amt[0], a);
        let set = |word: u32| {
            core.calls().iter().rev().find_map(|c| match c {
                Call::Set { slot, value }
                    if *slot == crate::dsp::fx_slot(&core, 0, word).unwrap() =>
                {
                    Some(*value)
                }
                _ => None,
            })
        };
        assert_eq!(
            set(dsp::FXW_AMT0),
            Some(127 << 16),
            "track 3 at full send, live on the link"
        );
        assert_eq!(set(dsp::FXW_AMT0 + 2), Some(127 << 16), "track 11");
        // track 3 off: its amount follows at the next pass
        a[2] = 0;
        give(Some(a));
        tick(&core);
        assert_eq!(set(dsp::FXW_AMT0), Some(0));
        // the other send effect takes the first cell and its stored amounts
        apply(&core, 1, 2, b"SND\0".as_ptr());
        assert_eq!((s.route[1], s.cell[1], s.row[0], used()), (1, 1, 2, 1));
        assert_eq!((d.fx_route[1], d.fx_amt[1]), (1, [50 << 8, 0, 0, 0]));
        assert!(
            !core
                .calls()
                .iter()
                .any(|c| matches!(c, Call::SettingsPut(k, _) if k.starts_with(b"fx/send/"))),
            "no amount is written to the settings"
        );
        // the phrase effect leaves: its bus goes, the cell stays the other effect's
        remove(&core, 0);
        assert_eq!((s.route[0], s.cell[0], s.row[0], used()), (0, 0, 2, 1));
        remove(&core, 1);
        assert_eq!(used(), 0);
        // an image without `phrase_sends`: the phrase effect takes a cell like any send effect
        crate::phrase::set_sends(0);
        assert!(!crate::phrase::sends_row(0));
        apply(&core, 0, 1, b"PRS\0".as_ptr());
        assert_eq!((s.route[0], s.cell[0], used()), (1, 1, 1));
        remove(&core, 0);
        crate::phrase::remove(1);
        give(None);
        clean();
    }
}

/// A send-only effect past the send slots has no cell and may not run on the master: its row runs on neither route (2), and so does every send-only
/// effect on an image without a send page.
#[test]
fn a_send_only_effect_without_a_cell_runs_on_neither_route() {
    let _g = crate::test_lock::hold();
    let send_only = FXF_SEND | FXF_NOMASTER | FXF_DEFAULT_SEND;
    unsafe {
        clean();
        let core = send_page_core(1);
        let d = crate::rows::frows();
        for (k, owner) in [(0usize, 1u8), (1, 2)] {
            d.fx_owner[k] = owner;
            d.fx_flags[k] = send_only;
        }
        apply(&core, 0, 1, b"SN1\0".as_ptr());
        apply(&core, 1, 2, b"SN2\0".as_ptr());
        let s = ss();
        assert_eq!(
            (s.route[0], s.cell[0], d.fx_route[0]),
            (1, 1, 1),
            "the one cell"
        );
        assert_eq!(
            (s.route[1], s.cell[1], d.fx_route[1]),
            (0, 0, 2),
            "past the slots: not run"
        );
        remove(&core, 0);
        remove(&core, 1);
        clean();
        // no send page: every effect on the master, a send-only one on neither route
        let bare = dkrt_test_support::FakeCore::new();
        d.fx_owner[0] = 1;
        d.fx_flags[0] = send_only;
        d.fx_owner[1] = 2;
        d.fx_flags[1] = FXF_SEND | FXF_DEFAULT_SEND;
        d.fx_route[1] = 1;
        apply(&bare, 0, 1, b"SN1\0".as_ptr());
        apply(&bare, 1, 2, b"SND\0".as_ptr());
        assert_eq!((d.fx_route[0], d.fx_route[1]), (2, 0));
        assert_eq!((ss().route[0], ss().route[1]), (0, 0));
        clean();
    }
}

/// FX ROUTING offers SEND against the send slots: the phrase effect's send takes no cell, so it leaves the slots to the other effects, and SEND stays open to
/// it while the image has a send page. Without `phrase_sends` it holds a cell and counts like any send effect.
#[test]
fn the_phrase_effect_takes_no_send_slot_from_the_routes_offered() {
    let _g = crate::test_lock::hold();
    let both = ROUTE_MASTER | ROUTE_SEND;
    unsafe {
        clean();
        let core = send_page_core(1);
        let s = ss();
        for (k, owner, allow, dflt) in [
            (0usize, 1u8, ROUTE_SEND, ROUTE_SEND),
            (1, 2, both, ROUTE_MASTER),
        ] {
            crate::fxpage::set_page_owner_for_test(k, owner);
            s.id[k][..2].copy_from_slice(&[b'm', b'0' + k as u8]);
            s.idlen[k] = 2;
            s.allow[k] = allow;
            s.dflt[k] = dflt;
        }
        // row 0 is PRISM, saved on its bus (its default); row 1 is saved on the master
        crate::phrase::reserve(1, 0);
        crate::phrase::set_sends(phrase_sends as *const () as usize);
        assert_eq!(saved_sends(&core), 1);
        let other = effect(&core, 1).unwrap();
        assert!(
            other.send_allowed,
            "the one slot is free: the phrase effect holds no cell"
        );
        let prism = effect(&core, 0).unwrap();
        assert!(prism.send_allowed && !prism.master_allowed);
        assert_eq!(prism.saved, Route::Send);
        // without `phrase_sends` the phrase effect is a send effect with a cell: the slot is its
        crate::phrase::set_sends(0);
        assert!(!effect(&core, 1).unwrap().send_allowed);
        assert!(
            effect(&core, 0).unwrap().send_allowed,
            "its own saved send keeps SEND open"
        );
        for k in [0, 1] {
            crate::fxpage::set_page_owner_for_test(k, 0);
        }
        clean();
    }
}

#[test]
fn route_choices_use_pending_sends_and_the_image_slot_limit() {
    let both = ROUTE_MASTER | ROUTE_SEND;
    assert_eq!(allowed_choices(both, 2, 0, 0), ROUTE_MASTER.into());
    assert_eq!(allowed_choices(both, 2, 1, 2), both.into());
    assert_eq!(allowed_choices(both, 2, 1, 1), ROUTE_MASTER.into());
    assert_eq!(allowed_choices(both, 2, 5, 1), both.into());
    assert_eq!(allowed_choices(ROUTE_SEND, 2, 1, 1), 0);
    assert_eq!(allowed_choices(ROUTE_MASTER, 2, 0, 2), ROUTE_MASTER.into());
}

/// The describe words of an effect module: one word for each of its rows that has a page, in row order: the row (bits 0 to 7), then the route it runs on, the route the settings
/// give it, the routes it allows and its default, four bits each. A module with no such row gets no word.
#[test]
fn describe_gives_one_word_for_each_effect_row_of_the_consumer() {
    use dkrt_abi::provider::{ConsumerId, Describe, WordSink};
    use dkrt_test_support::FakeCore;
    let _g = crate::test_lock::hold();
    let core = FakeCore::new();
    let both = ROUTE_MASTER | ROUTE_SEND;
    unsafe {
        let s = ss();
        for (k, owner, allow, running_send) in [
            (0, 2u8, both, true),
            (2, 2, ROUTE_MASTER, false),
            (3, 3, both, false),
        ] {
            crate::fxpage::set_page_owner_for_test(k, owner);
            s.id[k][..2].copy_from_slice(&[b'm', b'0' + k as u8]);
            s.idlen[k] = 2;
            s.allow[k] = allow;
            s.dflt[k] = ROUTE_MASTER;
            s.route[k] = running_send as _;
        }
        // row 0 is saved as master while it runs on the send bus (a change waiting for its reload)
        core.settings
            .borrow_mut()
            .push((b"fx/route/m0".to_vec(), std::vec![0]));
        publish(&core);
    }
    let words = |who: u8| {
        let mut buf = [0u32; 8];
        let mut sink = WordSink::new(&mut buf);
        <crate::fx::Fx as Describe<FakeCore>>::describe(&core, ConsumerId::new(who), &mut sink);
        sink.words().to_vec()
    };
    let word = |row: u32, running: u8, saved: u8, allowed: u8, dflt: u8| {
        row | (running as u32) << 8
            | (saved as u32) << 12
            | (allowed as u32) << 16
            | (dflt as u32) << 20
    };
    assert_eq!(
        words(2),
        std::vec![
            word(0, ROUTE_SEND, ROUTE_MASTER, both, ROUTE_MASTER),
            word(2, ROUTE_MASTER, ROUTE_MASTER, ROUTE_MASTER, ROUTE_MASTER),
        ]
    );
    assert_eq!(
        words(3),
        std::vec![word(3, ROUTE_MASTER, ROUTE_MASTER, both, ROUTE_MASTER)]
    );
    assert_eq!(
        words(9),
        std::vec::Vec::<u32>::new(),
        "no row of that consumer: no section"
    );
    assert_eq!(
        words(0),
        std::vec::Vec::<u32>::new(),
        "a free row has owner 0, which is no consumer"
    );
    unsafe {
        for k in [0, 2, 3] {
            crate::fxpage::set_page_owner_for_test(k, 0);
        }
        crate::fxsend::reset_published();
    }
}

/// `describe` runs on the MODULES command handler's task and can be preempted by the task that removes or re-routes an effect. At each point where a task switch can fall in (each
/// time the interrupt mask is given back), the words are those of the state before or after the change, never the owner of one with the route or the limits of the other.
#[test]
fn describe_is_one_state_when_the_effect_changes_between_its_reads() {
    use dkrt_abi::provider::{ConsumerId, Describe, WordSink};
    use dkrt_test_support::FakeCore;
    let _g = crate::test_lock::hold();
    let both = ROUTE_MASTER | ROUTE_SEND;
    let words = |core: &FakeCore| {
        let mut buf = [0u32; 8];
        let mut sink = WordSink::new(&mut buf);
        <crate::fx::Fx as Describe<FakeCore>>::describe(core, ConsumerId::new(2), &mut sink);
        sink.words().to_vec()
    };
    let word = |running: u8, allowed: u8| {
        0u32 | (running as u32) << 8
            | (ROUTE_MASTER as u32) << 12
            | (allowed as u32) << 16
            | (ROUTE_MASTER as u32) << 20
    };
    // before: effect row 0 of record 1 runs on the send bus and allows both; after: another module (record 2) holds the row, on the master, allowing the master only
    fn state(owner: u8, send: bool, allow: u8) {
        unsafe {
            let s = ss();
            crate::fxpage::set_page_owner_for_test(0, owner);
            s.id[0][..2].copy_from_slice(b"m0");
            s.idlen[0] = 2;
            s.allow[0] = allow;
            s.dflt[0] = ROUTE_MASTER;
            s.route[0] = send as _;
        }
    }
    for at in 1..=6 {
        let core = FakeCore::new();
        state(2, true, both);
        unsafe { publish(&core) };
        assert_eq!(words(&core), std::vec![word(ROUTE_SEND, both)]);
        core.host.interrupt_at_restore(at, || {
            let core = FakeCore::new();
            state(3, false, ROUTE_MASTER);
            unsafe { publish(&core) };
        });
        let got = words(&core);
        assert!(
            got == std::vec![word(ROUTE_SEND, both)] || got.is_empty(),
            "restore {at}: {got:x?} is a mix of the two states"
        );
        unsafe {
            crate::fxpage::set_page_owner_for_test(0, 0);
            crate::fxsend::reset_published();
        }
    }
}

/// `rows::reset` is the host tests' return to the start: the table published before it is gone too, so a later `describe` does not answer with the effects of the run before.
#[test]
fn reset_forgets_the_published_effect_words() {
    use dkrt_abi::provider::{ConsumerId, Describe, WordSink};
    use dkrt_test_support::FakeCore;
    let _g = crate::test_lock::hold();
    let core = FakeCore::new();
    unsafe {
        let s = ss();
        crate::fxpage::set_page_owner_for_test(0, 2);
        s.allow[0] = ROUTE_MASTER;
        s.dflt[0] = ROUTE_MASTER;
        publish(&core);
    }
    let words = |core: &FakeCore| {
        let mut buf = [0u32; 8];
        let mut sink = WordSink::new(&mut buf);
        <crate::fx::Fx as Describe<FakeCore>>::describe(core, ConsumerId::new(2), &mut sink);
        sink.words().len()
    };
    assert_eq!(words(&core), 1, "published");
    crate::rows::reset();
    assert_eq!(words(&core), 0, "reset forgot the published table");
    unsafe { crate::fxpage::set_page_owner_for_test(0, 0) };
}
