use super::*;
use crate::instrument_admission::{prepare_packed, Limits, Range};
use crate::instrument_load::{Ledger, Loads};
use crate::instrument_registry::Request;
pub(crate) fn limits() -> descriptor::Limits {
    descriptor::Limits {
        state_bytes: 1024,
        cycles_process: 100,
        cycles_reset: 100,
        call_depth: 2,
        stack_bytes: 32,
    }
}
fn put(bytes: &mut [u8], at: usize, value: u32) {
    bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
}
/// The canonical V2 section (`dkneeds::fixtures`): the same bytes the needs reader's and patchkit's tests read.
pub(crate) fn fixture() -> [u8; 208] {
    dkneeds::fixtures::v2_section()
}

/// No admission limit of a device: what is left is the format.
fn no_limits() -> descriptor::Limits {
    descriptor::Limits {
        state_bytes: u32::MAX,
        cycles_process: u32::MAX,
        cycles_reset: u32::MAX,
        call_depth: u32::MAX,
        stack_bytes: u32::MAX,
    }
}

/// The needs reader (`dkneeds`, the host tools and the runtime's scan) and this validator (the loader) read one format. For every one-word
/// edit of the canonical section: a section the loader admits has needs, and they are what the loader takes; and a section with needs that
/// the loader refuses is refused for something only the loader judges, never for the layout, the records, the text or the descriptor.
#[test]
fn the_needs_reader_and_the_loader_agree_on_every_one_word_edit() {
    let good = fixture();
    let needs = dkneeds::instrument_section_needs(&good).unwrap();
    let section = Section::parse(&good, dkneeds::fixtures::V2_DEVICE, no_limits()).unwrap();
    assert_eq!(
        (needs.rows, needs.tracks, needs.state_bytes),
        (1, 2, section.memory_request().state_bytes)
    );
    let (mut admitted, mut loader_only) = (0, 0);
    for at in (0..good.len()).step_by(4) {
        let old = u32::from_be_bytes(good[at..at + 4].try_into().unwrap());
        for value in [
            0,
            1,
            2,
            3,
            4,
            6,
            7,
            8,
            12,
            16,
            17,
            32,
            48,
            64,
            80,
            144,
            208,
            0x100,
            0x0100_0000,
            0x0700_0000,
            old ^ 1,
            old ^ 0x0100_0000,
            old.wrapping_add(4),
            old.wrapping_add(8),
            u32::MAX,
        ] {
            if value == old {
                continue;
            }
            let mut bytes = good;
            put(&mut bytes, at, value);
            let needs = dkneeds::instrument_section_needs(&bytes);
            match Section::parse(&bytes, dkneeds::fixtures::V2_DEVICE, no_limits()) {
                Ok(section) => {
                    admitted += 1;
                    let needs = needs.unwrap_or_else(|e| panic!("word {at:#x} = {value:#x}: the loader admits it, the needs reader says {e:?}"));
                    let tracks: u32 = section
                        .records()
                        .iter()
                        .flatten()
                        .map(|r| r.descriptor.tracks)
                        .sum();
                    assert_eq!(
                        (needs.rows, needs.tracks, needs.state_bytes),
                        (1, tracks, section.memory_request().state_bytes),
                        "word {at:#x} = {value:#x}"
                    );
                }
                Err(refusal) if needs.is_ok() => {
                    loader_only += 1;
                    assert!(
                        matches!(
                            refusal,
                            Error::Device
                                | Error::Header
                                | Error::Import
                                | Error::Relocation
                                | Error::Descriptor(descriptor::Error::Relocation)
                                | Error::Descriptor(descriptor::Error::Records)
                        ),
                        "word {at:#x} = {value:#x}: the needs reader takes it, the loader says {refusal:?}"
                    );
                }
                Err(_) => {}
            }
        }
    }
    // the edits reach both kinds: sections that still load, and ones only the loader refuses
    assert!(
        admitted > 0 && loader_only > 0,
        "{admitted} admitted, {loader_only} refused by the loader alone"
    );
}
#[test]
fn instrument_section_composes_parsing_reservation_and_admission() {
    let bytes = fixture();
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    assert_eq!(&section.imports()[..5], b"test\0");
    let record = section.records()[0].unwrap();
    let mut loads = Loads::new(16);
    let ticket = loads
        .reserve(
            0,
            &[Request {
                type_no: record.key,
                tracks: record.descriptor.tracks,
            }],
        )
        .unwrap();
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let grant = loads
        .reserve_memory(ticket, &mut ledger, &section.memory_request())
        .unwrap();
    let cap = Limits {
        l2: Range {
            start: 0x20000000,
            end: 0x20080000,
        },
        reset_frame: 1000,
        reset_overhead: 500,
        process_total: 100,
        call_depth: 2,
        stack_bytes: 32,
    };
    let plan = prepare_packed(&loads, ticket, &grant, &section.packed(&[20]), &cap).unwrap();
    assert_eq!(plan.rows()[0].unwrap().words[4], 0x20010048);
    assert_eq!(ledger.lengths[0], 128);
    assert!(loads.lookup(20).is_none());
}
#[test]
fn instrument_section_rejects_aliases_bad_counts_imports_and_relocations() {
    let good = fixture();
    for (at, value) in [
        (0, 0),
        (4, 5),
        (8, 2),
        (0x3c, 1),
        (0x0c, 0),
        (0x10, u32::MAX - 3),
        (0x14, 3),
        (0x18, 128),
        (0x24, 0),
        (0x28, 33),
        (0x20, 4097),
        (0x30, 1),
        (0x38, 1),
        (0x44, 0),
        (0x4c, 33),
        (112, 8),
        (112, 16),
        (112, u32::MAX),
        (116, 0x20000000),
        (116, 0x03000000),
        (116, 0x02000001),
        (116, 0x02010000),
        (116, 0x01000000),
    ] {
        let mut bad = good;
        put(&mut bad, at, value);
        assert!(
            Section::parse(&bad, 1, limits()).is_err(),
            "field {at:x} = {value:x}"
        );
    }
    for index in [80, 85, 111] {
        let mut bad = good;
        bad[index] = if index == 80 { 0 } else { 1 };
        assert!(matches!(
            Section::parse(&bad, 1, limits()),
            Err(Error::Import)
        ));
    }
}
#[test]
fn instrument_section_all_truncations_and_byte_edits_remain_bounded() {
    let good = fixture();
    for len in 0..good.len() {
        assert!(Section::parse(&good[..len], 1, limits()).is_err());
    }
    for i in 0..good.len() {
        let mut changed = good;
        changed[i] ^= 0xff;
        let _ = Section::parse(&changed, 1, limits());
    }
    let mut trailing = [0; 209];
    trailing[..208].copy_from_slice(&good);
    assert!(matches!(
        Section::parse(&trailing, 1, limits()),
        Err(Error::Layout)
    ));
}

#[test]
fn instrument_relocation_resolves_once_preserves_opcodes_and_source() {
    let mut bytes = fixture();
    bytes[144..150].copy_from_slice(&[0x11, 0x22, 0, 0x44, 4, 0]);
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    let mut out = [0xaa; 64];
    let mut calls = 0;
    section
        .relocate_into(0x20010000, &mut out, |name| {
            assert_eq!(name, b"test");
            calls += 1;
            Some(0xb80000)
        })
        .unwrap();
    assert_eq!(calls, 1);
    assert_eq!(&out[..6], &[0x11, 0x22, 0xb8, 0x44, 4, 0]);
    assert_eq!(&out[6..], &bytes[150..]);
    assert_eq!(&bytes[144..150], &[0x11, 0x22, 0, 0x44, 4, 0]);
}
#[test]
fn instrument_relocation_failures_leave_output_untouched() {
    let bytes = fixture();
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    let mut out = [0xaa; 64];
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| None),
        Err(RelocateError::MissingImport(0))
    );
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| Some(1 << 24)),
        Err(RelocateError::Address)
    );
    for base in [0x1ffffff0, 0x20010001, 0x20080000, 0xfffffff0] {
        assert_eq!(
            section.relocate_into(base, &mut out, |_| Some(0xb80000)),
            Err(RelocateError::Placement)
        );
    }
    assert_eq!(
        section.relocate_into(0x20010000, &mut out[..63], |_| Some(0)),
        Err(RelocateError::OutputSize)
    );
    assert_eq!(out, [0xaa; 64]);
}
#[test]
fn instrument_relocation_signed_import_addends_and_internal_overflow() {
    let mut bytes = fixture();
    put(&mut bytes, 116, 0x12000000); // abs32 import, encoded -4
    bytes[146..150].copy_from_slice(&[0xff, 0xff, 0xfc, 0xff]);
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    let mut out = [0xaa; 64];
    section
        .relocate_into(0x20010000, &mut out, |_| Some(0x20000100))
        .unwrap();
    assert_eq!(&out[2..6], &[0x00, 0x20, 0xfc, 0x00]);
    out.fill(0xaa);
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| Some(3)),
        Err(RelocateError::Address)
    );
    put(&mut bytes, 116, 0x11000000); // same bits as absolute internal data would overflow
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| Some(0)),
        Err(RelocateError::Address)
    );
    assert_eq!(out, [0xaa; 64]);
}

#[test]
fn instrument_relocation_late_failure_and_overlap_are_atomic() {
    let old = fixture();
    let mut bytes = [0u8; 224];
    bytes[..120].copy_from_slice(&old[..120]);
    bytes[128..152].copy_from_slice(&old[120..144]);
    bytes[160..].copy_from_slice(&old[144..]);
    for (at, value) in [
        (0x18, 160),
        (0x20, 2),
        (0x2c, 128),
        (0x34, 128),
        (0x40, 128),
        (0x48, 144),
        (120, 6),
        (124, 0x02000000),
    ] {
        put(&mut bytes, at, value);
    }
    bytes[168] = 0xff;
    bytes[170] = 0xff;
    bytes[171] = 0xff;
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    let mut out = [0xaa; 64];
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| Some(1)),
        Err(RelocateError::Address)
    );
    assert_eq!(out, [0xaa; 64]);
    put(&mut bytes, 120, 2);
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    assert_eq!(
        section.relocate_into(0x20010000, &mut out, |_| Some(1)),
        Err(RelocateError::Overlap)
    );
    assert_eq!(out, [0xaa; 64]);
}

#[test]
fn rejects_sample_and_old_experimental_formats() {
    for version in [5, 6, 7] {
        let mut bytes = fixture();
        put(&mut bytes, 4, version);
        assert!(matches!(
            Section::parse(&bytes, 1, limits()),
            Err(Error::Header)
        ));
    }
}
