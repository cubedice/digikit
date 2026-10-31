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
                bytes: 1024,
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
    let (mut loads, t, grant, _, _) = setup(300, 300);
    let image = [0x5a; 300];
    let receipt = crate::instrument_mailbox::Installed::test_receipt(t);
    let mut memory = [0xee; 640];
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
    assert_eq!(writes, 4);
    assert_eq!(&memory[..300], &image);
    assert!(memory[300..600].iter().all(|&b| b == 0));
    assert!(memory[600..].iter().all(|&b| b == 0xee));
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
