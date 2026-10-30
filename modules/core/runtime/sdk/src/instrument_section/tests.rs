use super::*;
use crate::instrument_admission::{prepare_packed, Limits, Range};
use crate::instrument_load::{Ledger, Loads};
use crate::instrument_registry::Request;
fn limits() -> descriptor::Limits {
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
fn fixture() -> [u8; 208] {
    let mut bytes = [0; 208];
    for (at, value) in [
        (0, 0x444b4453),
        (4, 6),
        (8, 1),
        (0x0c, 64),
        (0x10, 8),
        (0x14, 16),
        (0x18, 144),
        (0x24, 80),
        (0x28, 1),
        (0x1c, 112),
        (0x20, 1),
        (0x2c, 120),
        (0x34, 120),
        (0x40, 120),
        (0x44, 1),
        (0x48, 136),
        (0x4c, 1),
        (112, 0),
        (116, 0x02000000),
        (120, 20),
        (124, 0),
        (128, 6),
        (132, 16),
        (136, 0),
        (140, 12),
    ] {
        put(&mut bytes, at, value);
    }
    bytes[80..84].copy_from_slice(b"test");
    for (i, value) in [descriptor::MAGIC, 2, 48, 0, 2, 12, 8, 32, 20, 10, 2, 32]
        .iter()
        .enumerate()
    {
        bytes[160 + i * 4..164 + i * 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes
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
