use super::*;

fn limits() -> Limits {
    Limits {
        state_bytes: 64,
        cycles_process: 100,
        cycles_reset: 50,
        call_depth: 2,
        stack_bytes: 32,
    }
}
fn fixture() -> ([u8; 16], [u8; 64]) {
    let mut image = [0u8; 64];
    let words = [MAGIC, VERSION, 48, 0, 2, 12, 8, 32, 100, 50, 2, 32];
    for (i, word) in words.iter().enumerate() {
        image[16 + i * 4..20 + i * 4].copy_from_slice(&word.to_le_bytes());
    }
    let mut table = [0; 16];
    for (i, word) in [20u32, 0, 6, 16].iter().enumerate() {
        table[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    (table, image)
}
#[test]
fn instrument_descriptor_decodes_mixed_endianness_and_stride() {
    let (table, image) = fixture();
    let r = records(&table, &image, &[], limits()).unwrap();
    let row = r[0].unwrap();
    assert_eq!((row.key, row.reset_offset, row.process_offset), (20, 0, 6));
    assert_eq!(
        (row.descriptor.state_bytes, row.descriptor.allocation_bytes),
        (12, 32)
    );
    assert!(r[1..].iter().all(Option::is_none));
    let mut reversed = table;
    for word in reversed.chunks_exact_mut(4) {
        word.reverse();
    }
    assert!(records(&reversed, &image, &[], limits()).is_err());
}
#[test]
fn instrument_descriptor_status_output_is_explicit_and_unknown_flags_fail() {
    let (table, mut image) = fixture();
    let flags = |image: &[u8]| records(&table, image, &[], limits()).unwrap()[0].unwrap().descriptor.flags;
    assert_eq!(flags(&image), 0);
    image[28..32].copy_from_slice(&STATUS_OUTPUT.to_le_bytes());
    assert_eq!(flags(&image), STATUS_OUTPUT);
    for flags in [2u32, 3, u32::MAX] {
        image[28..32].copy_from_slice(&flags.to_le_bytes());
        assert_eq!(records(&table, &image, &[], limits()), Err(Error::Flags));
    }
}
#[test]
fn instrument_descriptor_rejects_truncation_and_hostile_offsets() {
    let (table, image) = fixture();
    for len in 0..16 {
        assert!(records(&table[..len], &image, &[], limits()).is_err());
    }
    for len in 0..64 {
        assert!(records(&table, &image[..len], &[], limits()).is_err());
    }
    for (field, value) in [
        (0, u32::MAX),
        (4, 1),
        (4, 60),
        (8, u32::MAX),
        (12, 17),
        (12, 20),
        (12, u32::MAX - 3),
    ] {
        let mut invalid = table;
        invalid[field..field + 4].copy_from_slice(&value.to_be_bytes());
        assert!(records(&invalid, &image, &[], limits()).is_err());
    }
    for len in 1..8 {
        assert_eq!(
            records(&table, &image, &[0; 8][..len], limits()),
            Err(Error::Relocation)
        );
    }
    for offset in [12u32, 16, 62, u32::MAX] {
        let mut reloc = [0; 8];
        reloc[..4].copy_from_slice(&offset.to_be_bytes());
        assert_eq!(
            records(&table, &image, &reloc, limits()),
            Err(Error::Relocation)
        );
    }
    let mut reloc = [0; 8];
    reloc[..4].copy_from_slice(&10u32.to_be_bytes());
    assert!(records(&table, &image, &reloc, limits()).is_ok()); // touches, does not overlap
}
#[test]
fn instrument_descriptor_bounds_batches_and_total_state() {
    let (table, image) = fixture();
    let mut batch = [0; 32];
    batch[..16].copy_from_slice(&table);
    batch[16..].copy_from_slice(&table);
    assert_eq!(records(&batch, &image, &[], limits()), Err(Error::Records));
    batch[16..20].copy_from_slice(&21u32.to_be_bytes());
    assert_eq!(
        records(&batch, &image, &[], limits())
            .unwrap()
            .iter()
            .flatten()
            .count(),
        2
    );
    let mut small = limits();
    small.state_bytes = 63;
    assert_eq!(records(&batch, &image, &[], small), Err(Error::Allocation));
    assert_eq!(
        records(&[0; 144], &image, &[], limits()),
        Err(Error::Records)
    );
}

#[test]
fn instrument_executable_ranges_exclude_data_padding_and_cross_section_entries() {
    let (mut table, _) = fixture();
    let mut bytes = [0u8; 16];
    for (i, value) in [0u32, 12, 64, 76].iter().enumerate() {
        bytes[i * 4..i * 4 + 4].copy_from_slice(&value.to_be_bytes());
    }
    let ranges = executable_ranges(&bytes, 80).unwrap();
    assert!(validate_executables(&table, &ranges).is_ok());
    for offset in [8u32, 12, 16, 60, 72] {
        table[4..8].copy_from_slice(&offset.to_be_bytes());
        assert_eq!(
            validate_executables(&table, &ranges),
            Err(Error::Executable)
        );
    }
    table[4..8].copy_from_slice(&64u32.to_be_bytes());
    assert!(validate_executables(&table, &ranges).is_ok());
    table[12..16].copy_from_slice(&8u32.to_be_bytes());
    assert_eq!(
        validate_executables(&table, &ranges),
        Err(Error::Executable)
    );
    for (field, value) in [(0, 1u32), (4, 4), (4, 82), (8, 10), (12, 64)] {
        let mut bad = bytes;
        bad[field..field + 4].copy_from_slice(&value.to_be_bytes());
        assert_eq!(executable_ranges(&bad, 80), Err(Error::Executable));
    }
    for size in 0..8 {
        assert!(executable_ranges(&bytes[..size], 80).is_err());
    }
}
