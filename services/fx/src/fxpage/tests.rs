use super::*;
use crate::dsp as fxdsp;
use dkrt_abi::core_api::CoreApi;
use dkrt_test_support::{Call, FakeCore};

/// An effect whose values an image adapter holds: its knobs are the adapter's values and its MIX the record's default, whatever the kit holds (a kit
/// saved with other values for it changes nothing); every other effect is read from the kit, and so is every effect in an image without the adapter.
#[test]
fn held_values_come_from_the_adapter_and_never_from_the_kit() {
    unsafe extern "C" fn adapter(id: u32, page: u32, raw: *mut u16) -> i32 {
        if id != 0xca1b {
            return 0;
        }
        *raw = 0x7f00;
        *raw.add(1) = 0x0300;
        // the page of the effect's row comes with the question
        *raw.add(2) = page as u16;
        1
    }
    let _g = crate::test_lock::hold();
    // the names the image's info table has for the functions (the assembly test of the runtime reads the table)
    assert_eq!(HELD_HASH, dkneeds::keys::name_hash("fx_held_values"));
    assert_eq!(PAINT_HASH, dkneeds::keys::name_hash("loop_ui_paint"));
    unsafe {
        set_held(adapter as *const () as usize);
        let held = held_values(0xca1b, 15).expect("the adapter holds this effect");
        assert_eq!(&held[..4], &[0x7f00, 0x0300, 15, 0]);
        assert!(
            held_values(0x1234, 15).is_none(),
            "another effect is the kit's"
        );
        assert!(held_values(0, 15).is_none(), "a row without an effect");
        assert_eq!(
            source_raw(Some(&held), 0, 0x100, || panic!("the kit was read")),
            0x7f00
        );
        assert_eq!(
            source_raw(Some(&held), 1, 0, || panic!("the kit was read")),
            0x0300
        );
        assert_eq!(
            source_raw(Some(&held), WORDS - 1, 0x7f00, || panic!(
                "the kit was read"
            )),
            0x7f00,
            "MIX is the record's default"
        );
        assert_eq!(source_raw(None, 0, 0x100, || 0x2a00), 0x2a00);
        set_held(0);
        assert!(
            held_values(0xca1b, 15).is_none(),
            "no adapter in the image: the kit's values"
        );
    }
}

/// The file's start finds the optional functions of the image by name: an image without them leaves the painter word 0 (the hook paints the stock page) and
/// no holder (every effect is the kit's).
#[test]
fn an_image_without_the_functions_leaves_no_painter_and_no_holder() {
    let _g = crate::test_lock::hold();
    let core = FakeCore::new();
    unsafe {
        core::ptr::write_volatile(core::ptr::addr_of_mut!(DKRT_FXP_PAINT), 0x1234);
        set_held(0x5678);
        painter_setup(&core);
        held_setup(&core);
        assert_eq!(
            core::ptr::read_volatile(core::ptr::addr_of!(DKRT_FXP_PAINT)),
            0
        );
        assert!(held_values(0xca1b, 15).is_none());
    }
}

/// A knob of effect row 2 is declared with a default, then its value changes: the parameter row is written first, then the new value is the word's live value.
#[test]
fn a_knob_gets_its_row_then_a_change_is_set_on_the_link() {
    let mut s: std::boxed::Box<FxpState> = unsafe { std::boxed::Box::new(core::mem::zeroed()) };
    let mut core = FakeCore::new();
    core.set_dynfx(0, 0);
    // the parameter table of the firmware, as a fake: rows of 0x40 bytes at 0x1000, the mirror index in the second word
    let mut t = std::vec![0; 96];
    t[dkrt_svc_rows::dynreg::w::P_TABLE] = 0x1000;
    t[dkrt_svc_rows::dynreg::w::PF_SIZE] = 0x40;
    core.set_dynreg(t);
    unsafe {
        s.min[2][1] = 0f32.to_bits();
        s.span[2][1] = 1f32.to_bits();
        write_knob_row(&core, &mut s, 2, 1, 120, 7, 33, 0x4000);
        assert_eq!(
            (s.cur[2][1], s.def[2][1], s.dsp[2][1], s.want[2][1]),
            (0x4000, 0x4000, 0x4000, 0x4000)
        );
        assert!(set_knob(&core, &mut s, 2, 1, 0x7f00));
        assert_eq!(s.want[2][1], 0x7f00);
        // a knob is word 4 + j of its row, MIX word 13, whatever its place among the knobs; the rows are 24 words apart
        set_knob(&core, &mut s, 3, WORDS - 1, 0x7f00);
    }
    assert_eq!(
        unsafe { dkrt_abi::mem::FwMem::rd(&core, 0x1000 + 120 * 0x40 + 4) },
        33,
        "the effect knob's row has the kit's value index as its mirror index"
    );
    let calls: std::vec::Vec<Call> = core
        .calls()
        .into_iter()
        .filter(|c| !matches!(c, Call::Wr(..)))
        .collect();
    assert!(matches!(
        calls[0],
        Call::Set {
            slot: dkrt_abi::link::Slot { win: 2, word: 53 },
            ..
        }
    ));
    assert!(matches!(
        calls[1],
        Call::Set {
            slot: dkrt_abi::link::Slot { win: 2, word: 85 },
            ..
        }
    ));
}

/// The offsets that the device tests read from `DKRT_FXP` (`tests/regression/runtime/fx-pages-ui.ts`).
#[test]
fn the_layout_the_device_tests_read_is_unchanged() {
    assert_eq!(core::mem::offset_of!(FxpState, sent), 16);
    assert_eq!(core::mem::offset_of!(FxpState, errors), 20);
    assert_eq!(core::mem::offset_of!(FxpState, seeds), 24);
    assert_eq!(core::mem::offset_of!(FxpState, owner), 28);
    assert_eq!(core::mem::offset_of!(FxpState, cur), 36);
    assert_eq!(core::mem::offset_of!(FxpState, dsp), 292);
}

/// Several turns before the link is free end as one write of the newest value, and the word counts as the DSP's when the link confirms it.
#[test]
fn a_fast_turn_sends_the_last_value_and_the_dsp_value_follows_the_confirmation() {
    let mut s: std::boxed::Box<FxpState> = unsafe { std::boxed::Box::new(core::mem::zeroed()) };
    let mut core = FakeCore::new();
    core.set_dynfx(0, 0);
    unsafe {
        s.min[2][1] = 0f32.to_bits();
        s.span[2][1] = 1f32.to_bits();
        write_knob_row(&core, &mut s, 2, 1, 120, 7, 33, 0x4000);
        for n in [0x100, 0x200, 0x300] {
            set_knob(&core, &mut s, 2, 1, n);
        }
        for _ in 0..50 {
            core.link.borrow_mut().pump();
        }
        let slot = fxdsp::fx_slot(&core, 2, 5).unwrap();
        let wire = core.link.borrow().t.words();
        assert_eq!(wire.len(), 1);
        assert_eq!(
            core.link_confirmed(slot),
            Some(knob_value(s.min[2][1], s.span[2][1], 0x300))
        );
    }
}
