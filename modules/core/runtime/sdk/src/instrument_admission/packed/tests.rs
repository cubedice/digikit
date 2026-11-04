use super::*;
use crate::instrument_load::{Ledger, MemoryRequest};
use crate::instrument_registry::Request;

fn fixture() -> ([u8; 128], [u8; 32], [u8; 16]) {
    let mut image = [0; 128];
    for (offset, tracks) in [(16, 2), (80, 1)] {
        let words: [u32; 12] = [
            descriptor::MAGIC,
            2,
            48,
            0,
            tracks,
            12,
            8,
            32,
            20,
            10,
            2,
            32,
        ];
        for (i, word) in words.iter().enumerate() {
            image[offset + i * 4..offset + i * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
    }
    let mut table = [0; 32];
    for (i, word) in [100u32, 0, 64, 16, 200, 6, 70, 80].iter().enumerate() {
        table[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    let mut text = [0; 16];
    for (i, word) in [0u32, 12, 64, 76].iter().enumerate() {
        text[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    (image, table, text)
}
fn limits() -> Limits {
    Limits {
        l2: Range {
            start: 0x20000000,
            end: 0x20080000,
        },
        reset_frame: 1000,
        reset_overhead: 500,
        process_total: 100,
        call_depth: 2,
        stack_bytes: 32,
    }
}
fn setup() -> (Loads, Ticket, Grant) {
    let mut loads = Loads::new(16);
    // Physical rows deliberately differ from record order.
    let t = loads
        .reserve(
            0,
            &[
                Request {
                    type_no: 21,
                    tracks: 1,
                },
                Request {
                    type_no: 20,
                    tracks: 2,
                },
            ],
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
        .reserve_memory(
            t,
            &mut ledger,
            &MemoryRequest {
                image_bytes: 128,
                bss_bytes: 8,
                state_bytes: 48,
                align: 16,
            },
        )
        .unwrap();
    (loads, t, grant)
}
#[test]
fn instrument_packed_admission_derives_split_text_and_aligned_state_rows() {
    let (loads, t, grant) = setup();
    let (image, records, executable) = fixture();
    let packed = Packed {
        image: &image,
        records: &records,
        relocations: &[],
        executable: &executable,
        resolved_types: &[20, 21],
    };
    let plan = prepare_packed(&loads, t, &grant, &packed, &limits()).unwrap();
    let second = plan.rows()[0].unwrap();
    let first = plan.rows()[1].unwrap();
    assert_eq!(first.words[3], 2);
    assert_eq!(first.words[4], 0x20010088);
    assert_eq!(second.words[4], first.words[4] + 32);
    assert_eq!(second.words[7], grant.state.end);
    assert_eq!(&first.words[8..11], &[0xb88000, 0xb88000, 0xb88006]);
    assert_eq!(first.process_sw, 0xb88020);
    assert_eq!(
        first.process_text,
        Range {
            start: 0x20010040,
            end: 0x2001004c
        }
    );
    assert!(loads.lookup(20).is_none());
}
#[test]
fn instrument_status_capability_follows_physical_row_mapping() {
    let (loads, ticket, grant) = setup();
    let (mut image, records, executable) = fixture();
    image[28..32].copy_from_slice(&descriptor::STATUS_OUTPUT.to_le_bytes());
    let packed = Packed {
        image: &image,
        records: &records,
        relocations: &[],
        executable: &executable,
        resolved_types: &[20, 21],
    };
    let plan = prepare_packed(&loads, ticket, &grant, &packed, &limits()).unwrap();
    assert_eq!(plan.rows()[0].unwrap().flags, 0);
    assert_eq!(plan.rows()[1].unwrap().flags, descriptor::STATUS_OUTPUT);
}
#[test]
fn instrument_packed_admission_rejects_mappings_tracks_and_grant_mismatch() {
    let (loads, t, mut grant) = setup();
    let (mut image, records, executable) = fixture();
    for types in [&[20u32][..], &[20, 20], &[21, 20], &[20, 22]] {
        let p = Packed {
            image: &image,
            records: &records,
            relocations: &[],
            executable: &executable,
            resolved_types: types,
        };
        assert!(matches!(
            prepare_packed(&loads, t, &grant, &p, &limits()),
            Err(Error::Types)
        ));
    }
    image[32..36].copy_from_slice(&1u32.to_le_bytes()); // descriptor tracks
    let p = Packed {
        image: &image,
        records: &records,
        relocations: &[],
        executable: &executable,
        resolved_types: &[20, 21],
    };
    assert!(matches!(
        prepare_packed(&loads, t, &grant, &p, &limits()),
        Err(Error::Types)
    ));
    grant.image.end += 8;
    assert!(matches!(
        prepare_packed(&loads, t, &grant, &p, &limits()),
        Err(Error::Grant)
    ));
    assert_eq!(loads.capacity().rows_used, 2);
}
#[test]
fn instrument_packed_admission_rejects_data_callbacks_and_started_loads() {
    let (mut loads, t, grant) = setup();
    let (image, mut records, executable) = fixture();
    records[8..12].copy_from_slice(&16u32.to_be_bytes());
    let p = Packed {
        image: &image,
        records: &records,
        relocations: &[],
        executable: &executable,
        resolved_types: &[20, 21],
    };
    assert!(matches!(
        prepare_packed(&loads, t, &grant, &p, &limits()),
        Err(Error::Descriptor(descriptor::Error::Executable))
    ));
    loads.start(t).unwrap();
    assert!(matches!(
        prepare_packed(&loads, t, &grant, &p, &limits()),
        Err(Error::Load(LoadError::WrongPhase))
    ));
}
