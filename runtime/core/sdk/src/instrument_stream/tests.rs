extern crate std;
use super::*;
use crate::instrument_image::CHUNK;
use crate::instrument_section::{tests::limits, RelocateError};
use std::vec::Vec;

const BASE: u32 = 0x20010000;
const LEN: usize = 4 * CHUNK + 1000;

fn put(bytes: &mut [u8], at: usize, value: u32) {
    bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
}

/// The canonical section (`dkneeds::fixtures::v2_section`) with an image of `image_len` bytes and the relocations `fixups` (offset, flags) in
/// the given order. A second text range covers the image after the descriptor (64..), where the fixups are. Returns the bytes and where the
/// image starts in them.
fn build(image_len: usize, fixups: &[(u32, u32)]) -> (Vec<u8>, usize) {
    let old = dkneeds::fixtures::v2_section();
    let records = 112 + fixups.len() * 8;
    let text = records + 16;
    let image_at = (text + 16 + 15) & !15;
    let mut bytes = std::vec![0u8; image_at + image_len];
    bytes[..112].copy_from_slice(&old[..112]);
    bytes[records..records + 16].copy_from_slice(&old[120..136]);
    for (at, value) in [
        (0x0c, image_len as u32),
        (0x18, image_at as u32),
        (0x20, fixups.len() as u32),
        (0x2c, records as u32),
        (0x34, records as u32),
        (0x40, records as u32),
        (0x48, text as u32),
        (0x4c, 2),
        (text, 0),
        (text + 4, 12),
        (text + 8, 64),
        (text + 12, image_len as u32),
    ] {
        put(&mut bytes, at, value);
    }
    for (i, &(at, flags)) in fixups.iter().enumerate() {
        put(&mut bytes, 112 + i * 8, at);
        put(&mut bytes, 116 + i * 8, flags);
    }
    // the canonical image (the two callbacks and the descriptor), then a pattern
    bytes[image_at..image_at + 64].copy_from_slice(&old[144..208]);
    for (i, b) in bytes[image_at + 64..].iter_mut().enumerate() {
        *b = (i * 7 + 3) as u8;
    }
    // small encoded values: every class relocates without overflow, and changes the instruction
    for &(at, _) in fixups {
        let at = image_at + at as usize;
        bytes[at + 2..at + 6].copy_from_slice(&[0, 0, 0x10, 0]);
    }
    (bytes, image_at)
}

/// Every kind and class of fixup, and one in each place a chunk boundary `b` can cut an instruction: ending on it (b - 6), its patched bytes
/// cut 2 | 2 (b - 4, both kinds), starting 2 before it so that all of its patched bytes are after it (b - 2), and starting on it (b).
fn fixups() -> Vec<(u32, u32)> {
    let b = |i: usize| (i * CHUNK) as u32;
    std::vec![
        (64, 0x0000_0000),
        (70, 0x1000_0000),
        (76, 0x1100_0000),
        (82, 0x0200_0000),
        (88, 0x1200_0000),
        (b(1) - 6, 0x1100_0000),
        (b(1), 0x0000_0000),
        (b(2) - 4, 0x1100_0000),
        (b(3) - 2, 0x1200_0000),
        (b(3) + 40, 0x0000_0000),
        (b(4) - 4, 0x0200_0000),
        (b(4) + 2, 0x1000_0000),
    ]
}

/// Reads the section from a copy; it can fail one read, or change one byte just before a given read (the file replaced while it is sent).
struct Mem {
    bytes: Vec<u8>,
    reads: u32,
    fail_at: Option<u32>,
    change: Option<(u32, usize, u8)>,
}
impl Mem {
    fn new(bytes: &[u8]) -> Self {
        Mem {
            bytes: bytes.to_vec(),
            reads: 0,
            fail_at: None,
            change: None,
        }
    }
}
impl Reader for Mem {
    fn read(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()> {
        if let Some((before, at, value)) = self.change {
            if before == self.reads {
                self.bytes[at] = value;
            }
        }
        self.reads += 1;
        if self.fail_at == Some(self.reads) {
            return Err(());
        }
        let at = offset as usize;
        out.copy_from_slice(self.bytes.get(at..at + out.len()).ok_or(())?);
        Ok(())
    }
}

fn stream_all(stream: &mut Stream<Mem>, len: usize) -> Result<Vec<u8>, ()> {
    let mut out = std::vec![0u8; len];
    for offset in (0..len).step_by(CHUNK) {
        let end = (offset + CHUNK).min(len);
        stream.fill(offset as u32, &mut out[offset..end])?;
    }
    Ok(out)
}

fn admitted(bytes: &[u8]) -> (Section<'_>, Relocation, Vec<u8>) {
    let section = Section::parse(bytes, 1, limits()).unwrap();
    let rel = section
        .prepare_relocation(BASE, |_| Some(0x2000), true)
        .unwrap();
    let mut copy = std::vec![0u8; section.memory_request().image_bytes as usize];
    section
        .relocate_into(BASE, &mut copy, |_| Some(0x2000))
        .unwrap();
    (section, rel, copy)
}

#[test]
fn a_streamed_image_is_the_relocated_copy_chunk_by_chunk() {
    let fixups = fixups();
    let (bytes, image_at) = build(LEN, &fixups);
    let (section, rel, copy) = admitted(&bytes);
    for &(at, _) in &fixups {
        let at = at as usize;
        assert_ne!(
            copy[at..at + 6],
            bytes[image_at + at..image_at + at + 6],
            "the fixup at {at} changes its instruction"
        );
    }
    let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
    assert_eq!(stream.image_len(), LEN as u32);
    assert!(!stream.complete(), "nothing streamed yet");
    assert_eq!(stream_all(&mut stream, LEN).unwrap(), copy);
    assert!(stream.complete());
}

#[test]
fn an_image_that_ends_on_a_chunk_and_one_with_no_fixup_stream_too() {
    let mut ends_on_a_chunk = fixups()[..7].to_vec();
    ends_on_a_chunk.push(((2 * CHUNK - 6) as u32, 0x1100_0000));
    for (len, fixups) in [(2 * CHUNK, ends_on_a_chunk), (CHUNK + 64, std::vec![])] {
        let (bytes, _) = build(len, &fixups);
        let (section, rel, copy) = admitted(&bytes);
        let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
        assert_eq!(stream_all(&mut stream, len).unwrap(), copy);
        assert!(stream.complete());
    }
}

/// The safety rule: a stream that completes gave exactly the admitted image, relocated. A change the stream reads (in the table, or in the image
/// around every boundary and fixup) fails it or keeps it from completing; only a change after its last read of that byte can complete.
#[test]
fn a_file_changed_after_the_admission_or_while_it_is_sent_never_completes_wrong() {
    let fixups = fixups();
    let (bytes, image_at) = build(LEN, &fixups);
    let (section, rel, copy) = admitted(&bytes);
    let mut at: Vec<usize> = (112..112 + fixups.len() * 8).collect();
    for &(f, _) in &fixups {
        at.extend((image_at + f as usize).saturating_sub(2)..image_at + f as usize + 8);
    }
    for b in 1..=4 {
        at.extend(image_at + b * CHUNK - 8..image_at + b * CHUNK + 8);
    }
    at.extend([image_at + 100, image_at + 2000, image_at + LEN - 1]);
    let (mut failed, mut completed) = (0, 0);
    for &byte in &at {
        for flip in [0x01, 0x80] {
            for before in [0, 1, 2, 3, 5, 8, 12, 17, 23, 30, 40, 60] {
                let mut mem = Mem::new(&bytes);
                mem.change = Some((before, byte, bytes[byte] ^ flip));
                let mut stream = Stream::new(&section, rel, mem);
                match stream_all(&mut stream, LEN) {
                    Ok(out) if stream.complete() => {
                        assert_eq!(out, copy, "byte {byte} changed before read {before}");
                        assert_ne!(
                            before, 0,
                            "a change before the first read is always seen (byte {byte})"
                        );
                        completed += 1;
                    }
                    _ => {
                        assert!(!stream.complete());
                        failed += 1;
                    }
                }
            }
        }
    }
    assert!(failed > 0 && completed > 0);
}

#[test]
fn a_failed_read_or_a_chunk_out_of_order_fails_the_stream_for_good() {
    let (bytes, _) = build(LEN, &fixups());
    let (section, rel, _) = admitted(&bytes);
    let mut reads = 0;
    let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
    stream_all(&mut stream, LEN).unwrap();
    for fail_at in 1..=stream.reader.reads {
        let mut mem = Mem::new(&bytes);
        mem.fail_at = Some(fail_at);
        let mut stream = Stream::new(&section, rel, mem);
        assert!(stream_all(&mut stream, LEN).is_err());
        assert!(!stream.complete());
        let mut out = [0u8; CHUNK];
        assert_eq!(
            stream.fill(0, &mut out),
            Err(()),
            "a failed stream stays failed"
        );
        reads += 1;
    }
    assert!(reads > 10);
    let mut out = [0u8; CHUNK];
    let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
    assert_eq!(
        stream.fill(CHUNK as u32, &mut out),
        Err(()),
        "not the next chunk"
    );
    assert_eq!(stream.fill(0, &mut out), Err(()));
    let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
    stream.fill(0, &mut out).unwrap();
    assert_eq!(stream.fill(0, &mut out), Err(()), "a chunk again");
    let mut stream = Stream::new(&section, rel, Mem::new(&bytes));
    assert_eq!(
        stream.fill(LEN as u32 - 8, &mut out[..16]),
        Err(()),
        "past the image"
    );
}

#[test]
fn a_streamed_admission_needs_the_table_in_order() {
    let mut swapped = fixups();
    swapped.swap(1, 2);
    let (bytes, _) = build(LEN, &swapped);
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    assert_eq!(
        section.prepare_relocation(BASE, |_| Some(0x2000), true),
        Err(RelocateError::Order)
    );
    assert!(section
        .prepare_relocation(BASE, |_| Some(0x2000), false)
        .is_ok());
    // two instructions that overlap are refused either way
    let mut overlapping = fixups();
    overlapping[1].0 = overlapping[0].0 + 4;
    let (bytes, _) = build(LEN, &overlapping);
    let section = Section::parse(&bytes, 1, limits()).unwrap();
    assert_eq!(
        section.prepare_relocation(BASE, |_| Some(0x2000), true),
        Err(RelocateError::Order)
    );
    assert_eq!(
        section.prepare_relocation(BASE, |_| Some(0x2000), false),
        Err(RelocateError::Overlap)
    );
}
