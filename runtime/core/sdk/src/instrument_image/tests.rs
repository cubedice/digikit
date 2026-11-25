use super::*;
use crate::instrument_load::{Ledger, MemoryRequest};
use crate::instrument_registry::Request;

fn setup(image_bytes: u32, bss_bytes: u32) -> (Loads, Ticket, Grant, [u32; 2], [u32; 2]) {
    let mut loads = Loads::new(16);
    let ticket = loads
        .reserve(
            0,
            &[Request {
                type_no: 20,
                tracks: 2,
            }],
        )
        .unwrap();
    let mut offsets = [0, 0];
    let mut lengths = [0, 0];
    let grant = loads
        .reserve_memory(
            ticket,
            &mut Ledger {
                base: 0x20010000,
                bytes: if image_bytes > 1024 {
                    (CHUNK * 3) as u32
                } else {
                    1024
                },
                offsets: &mut offsets,
                lengths: &mut lengths,
            },
            &MemoryRequest {
                image_bytes,
                bss_bytes,
                state_bytes: 32,
                align: 16,
            },
        )
        .unwrap();
    (loads, ticket, grant, offsets, lengths)
}
#[test]
fn instrument_image_retries_and_clears_bss_before_initialization() {
    let (mut loads, t, grant, _, _) = setup((CHUNK + 44) as u32, 300);
    let image = [0x5a; CHUNK + 44];
    let receipt = crate::instrument_mailbox::Installed::test_receipt(t);
    let mut memory = [0xee; CHUNK + 384];
    let mut transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    assert_eq!(transfer.finish(&mut loads, &receipt), Err(Error::NotReady));
    let mut writes = 0;
    while let Some(write) = transfer.next(&loads).unwrap() {
        assert_eq!(transfer.next(&loads).unwrap().unwrap(), write);
        let offset = (write.ack.address - grant.image.start) as usize;
        memory[offset..offset + write.data.len()].copy_from_slice(write.data);
        let ack = write.ack;
        transfer.acknowledged(&loads, ack).unwrap();
        assert_eq!(transfer.acknowledged(&loads, ack), Err(Error::WrongAck));
        writes += 1;
    }
    assert_eq!(writes, 3);
    assert_eq!(&memory[..image.len()], &image);
    assert!(memory[image.len()..image.len() + 300]
        .iter()
        .all(|&b| b == 0));
    assert!(memory[image.len() + 300..].iter().all(|&b| b == 0xee));
    assert!(transfer.install_ticket(&loads).is_ok());
    let other = loads
        .reserve(
            1,
            &[Request {
                type_no: 21,
                tracks: 1,
            }],
        )
        .unwrap();
    let wrong = crate::instrument_mailbox::Installed::test_receipt(other);
    assert_eq!(transfer.finish(&mut loads, &wrong), Err(Error::NotReady));
    assert_eq!(
        transfer.finish(&mut loads, &receipt),
        Ok(Command::Initialize(t))
    );
    assert_eq!(transfer.next(&loads), Err(Error::Stopped));
    assert!(loads.lookup(20).is_none());
}
#[test]
fn instrument_image_binding_and_ack_validation_do_not_advance_on_errors() {
    let (mut loads, t, grant, _, _) = setup(8, 4);
    let image = [1; 8];
    assert!(matches!(
        Transfer::begin(&mut loads, t, &grant, &image, false),
        Err(Error::NotDrained)
    ));
    assert!(matches!(
        Transfer::begin(&mut loads, t, &grant, &image[..4], true),
        Err(Error::ImageSize)
    ));
    let forged = Grant {
        ticket: t,
        image: grant.image,
        state: crate::instrument_admission::Range {
            start: grant.state.start + 8,
            end: grant.state.end + 8,
        },
    };
    assert!(matches!(
        Transfer::begin(&mut loads, t, &forged, &image, true),
        Err(Error::Load(LoadError::Invalid))
    ));
    let mut transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    let original = transfer.next(&loads).unwrap().unwrap().ack;
    for ack in [
        Ack {
            address: original.address + 4,
            ..original
        },
        Ack {
            bytes: 4,
            ..original
        },
    ] {
        assert_eq!(transfer.acknowledged(&loads, ack), Err(Error::WrongAck));
        assert_eq!(transfer.next(&loads).unwrap().unwrap().ack, original);
    }
    transfer.acknowledged(&loads, original).unwrap();
    let zero = transfer.next(&loads).unwrap().unwrap();
    assert_eq!(zero.data, &[0; 8]); // four bytes BSS plus four bytes alignment
}
#[test]
fn instrument_image_failure_keeps_allocation_until_drained_detach() {
    let (mut loads, t, grant, mut offsets, mut lengths) = setup(8, 0);
    let image = [1; 8];
    let mut transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    let pending = transfer.next(&loads).unwrap().unwrap().ack;
    assert_eq!(transfer.failed(&mut loads), Ok(Command::Detach(t)));
    assert_eq!(transfer.acknowledged(&loads, pending), Err(Error::Stopped));
    assert_eq!(transfer.next(&loads), Err(Error::Stopped));
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 1024,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    assert!(loads.release_memory(t, &mut ledger).is_err());
    assert_eq!(ledger.lengths[0], 64);
    // Caller reports drained transport plus completed DSP detach, never a timeout.
    loads.detached(t).unwrap();
    loads.release_memory(t, &mut ledger).unwrap();
    assert_eq!(ledger.lengths[0], 0);
}
#[test]
fn instrument_image_external_cancellation_stops_further_writes() {
    let (mut loads, t, grant, _, _) = setup(8, 0);
    let image = [1; 8];
    let transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    loads.cancel(t).unwrap();
    assert_eq!(
        transfer.next(&loads),
        Err(Error::Load(LoadError::WrongPhase))
    );
}

/// Gives `image` chunk by chunk, noting the offsets asked for; fails the `fail`th fill (after scribbling on the chunk) and answers `complete`
/// with `whole`.
struct Chunks<'a> {
    image: &'a [u8],
    fills: [u32; 8],
    n: usize,
    fail: Option<usize>,
    whole: bool,
    completes: u32,
}
impl<'a> Chunks<'a> {
    fn new(image: &'a [u8]) -> Self {
        Chunks {
            image,
            fills: [u32::MAX; 8],
            n: 0,
            fail: None,
            whole: true,
            completes: 0,
        }
    }
}
impl Source for Chunks<'_> {
    fn fill(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()> {
        self.fills[self.n] = offset;
        self.n += 1;
        if self.fail == Some(self.n) {
            out.fill(0xee);
            return Err(());
        }
        out.copy_from_slice(&self.image[offset as usize..offset as usize + out.len()]);
        Ok(())
    }
    fn complete(&mut self) -> bool {
        self.completes += 1;
        self.whole
    }
}

#[test]
fn instrument_image_streamed_sends_what_a_copy_sends_one_fill_per_chunk() {
    let mut image = [0u8; 2 * CHUNK + 44];
    for (i, b) in image.iter_mut().enumerate() {
        *b = (i * 13) as u8;
    }
    let (mut loads, t, grant, _, _) = setup(image.len() as u32, 300);
    let mut copied = [0u8; 3 * CHUNK];
    let mut n = 0;
    let mut transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    while let Some(write) = transfer.next(&loads).unwrap() {
        copied[n..n + write.data.len()].copy_from_slice(write.data);
        n += write.data.len();
        transfer.acknowledged(&loads, write.ack).unwrap();
    }
    let (mut loads, t, grant, _, _) = setup(image.len() as u32, 300);
    let mut source = Chunks::new(&image);
    let mut chunk = [0u8; CHUNK];
    let mut transfer = Transfer::begin_streamed(
        &mut loads,
        t,
        &grant,
        &mut source,
        image.len() as u32,
        &mut chunk,
        true,
    )
    .unwrap();
    let mut streamed = [0u8; 3 * CHUNK];
    let mut m = 0;
    while let Some(write) = transfer.next(&loads).unwrap() {
        // a retry before the ACK sends the same bytes, and asks the source for nothing
        assert_eq!(transfer.next(&loads).unwrap().unwrap(), write);
        streamed[m..m + write.data.len()].copy_from_slice(write.data);
        m += write.data.len();
        let ack = write.ack;
        transfer.acknowledged(&loads, ack).unwrap();
    }
    assert!(transfer.install_ticket(&loads).is_ok());
    drop(transfer);
    assert_eq!((m, &streamed[..m]), (n, &copied[..n]));
    assert_eq!(
        &source.fills[..source.n],
        &[0, CHUNK as u32, 2 * CHUNK as u32]
    );
    assert_eq!(source.completes, 1);
}

#[test]
fn instrument_image_streamed_source_failure_offers_no_write_and_only_detaches() {
    let image = [7u8; CHUNK + 40];
    // the second chunk cannot be read; then: every chunk read, but not the admitted bytes
    for (fail, whole) in [(Some(2), true), (None, false)] {
        let (mut loads, t, grant, _, _) = setup(image.len() as u32, 300);
        let mut source = Chunks::new(&image);
        source.fail = fail;
        source.whole = whole;
        let mut chunk = [0u8; CHUNK];
        let mut transfer = Transfer::begin_streamed(
            &mut loads,
            t,
            &grant,
            &mut source,
            image.len() as u32,
            &mut chunk,
            true,
        )
        .unwrap();
        let first = transfer.next(&loads).unwrap().unwrap().ack;
        if fail.is_some() {
            // the second chunk is read when the first write is acknowledged
            assert_eq!(transfer.acknowledged(&loads, first), Err(Error::Source));
            let second = Ack {
                address: first.address + CHUNK as u32,
                bytes: 40,
                ..first
            };
            for ack in [first, second] {
                assert_eq!(transfer.acknowledged(&loads, ack), Err(Error::Source));
            }
        } else {
            transfer.acknowledged(&loads, first).unwrap();
            let last = transfer.next(&loads).unwrap().unwrap().ack;
            assert_eq!(transfer.acknowledged(&loads, last), Err(Error::Source));
        }
        assert_eq!(
            transfer.next(&loads),
            Err(Error::Source),
            "no write, not even the BSS"
        );
        assert_eq!(transfer.install_ticket(&loads), Err(Error::NotReady));
        assert_eq!(transfer.failed(&mut loads), Ok(Command::Detach(t)));
        assert_eq!(transfer.next(&loads), Err(Error::Stopped));
    }
}

#[test]
fn instrument_image_streamed_first_chunk_failure_leaves_the_load_unstarted() {
    let image = [7u8; 64];
    let (mut loads, t, grant, _, _) = setup(64, 8);
    let mut source = Chunks::new(&image);
    source.fail = Some(1);
    let mut chunk = [0u8; CHUNK];
    assert!(matches!(
        Transfer::begin_streamed(&mut loads, t, &grant, &mut source, 64, &mut chunk, true),
        Err(Error::Source)
    ));
    let mut source = Chunks::new(&image);
    assert!(matches!(
        Transfer::begin_streamed(&mut loads, t, &grant, &mut source, 60, &mut chunk, true),
        Err(Error::ImageSize)
    ));
    assert_eq!(source.n, 0, "a wrong size reads nothing");
    // not started: the same load can still begin
    let transfer = Transfer::begin(&mut loads, t, &grant, &image, true).unwrap();
    assert!(transfer.next(&loads).unwrap().is_some());
}
