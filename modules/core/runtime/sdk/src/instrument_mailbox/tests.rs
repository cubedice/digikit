use super::*;
use crate::instrument_registry::Request;

fn reserve(loads: &mut Loads, owner: u32, types: &[u32]) -> Ticket {
    let mut requests = [Request {
        type_no: 0,
        tracks: 2,
    }; 8];
    for (i, type_no) in types.iter().enumerate() {
        requests[i].type_no = *type_no;
    }
    loads.reserve(owner, &requests[..types.len()]).unwrap()
}

fn initialize(loads: &mut Loads, ticket: Ticket) -> Command {
    loads.start(ticket).unwrap();
    loads.image_acked(ticket).unwrap()
}

fn checksum(reply: &mut [u32; 16]) {
    let (mut a, mut b) = (0u32, 0u32);
    for word in &reply[..15] {
        a = a.wrapping_add(*word);
        b = b.wrapping_add(a);
    }
    reply[15] = a ^ b;
}

fn response(mailbox: &Mailbox, status: u32, capabilities: u32) -> [u32; 16] {
    let p = mailbox.pending.unwrap();
    let mut words = [0; 16];
    words[0] = 0x494ea101;
    words[4..12].copy_from_slice(&[
        0x49525231,
        p.sequence,
        p.ticket.owner(),
        p.ticket.generation(),
        p.operation,
        p.row,
        status,
        capabilities,
    ]);
    checksum(&mut words);
    words
}

fn transfer(mailbox: &mut Mailbox) -> [u32; 16] {
    let mut destination = [u32::MAX; 16];
    for stage in [CLEAR, BODY, COMMIT] {
        let write = mailbox.write().unwrap();
        assert_eq!(write.stage, stage);
        assert_eq!(mailbox.write(), Some(write)); // retry is byte-identical
        destination[write.offset as usize..write.offset as usize + write.count]
            .copy_from_slice(&write.words[..write.count]);
        if stage != COMMIT {
            assert_eq!(destination[0], 0);
        }
        mailbox.write_acked(write.sequence, write.stage).unwrap();
        assert_eq!(
            mailbox.write_acked(write.sequence, write.stage),
            Err(Error::WrongTransfer)
        );
    }
    assert_eq!(destination.iter().fold(0, |a, b| a ^ b), 0);
    assert_eq!(mailbox.write(), None);
    destination
}

#[test]
fn every_noncontiguous_row_must_complete_before_publish_or_release() {
    let mut loads = Loads::new(32);
    let hole = reserve(&mut loads, 1, &[16]);
    let other = reserve(&mut loads, 2, &[17]);
    loads.cancel(hole).unwrap();
    let ticket = reserve(&mut loads, 3, &[18, 19]); // physical rows 0 and 2
    let mut mailbox = Mailbox::new();
    let command = initialize(&mut loads, ticket);
    mailbox.begin(command, &loads, true).unwrap();
    assert_eq!(transfer(&mut mailbox)[6], 0);
    let old = response(&mailbox, 0, 1);
    assert_eq!(mailbox.reply(&old, &mut loads), Ok(None));
    assert_eq!(loads.lookup(18), None);
    assert_eq!(transfer(&mut mailbox)[6], 2);
    assert_eq!(mailbox.reply(&old, &mut loads), Err(Error::MismatchedReply));
    let reply = response(&mailbox, 0, 1);
    assert_eq!(
        mailbox.reply(&reply, &mut loads),
        Ok(Some(Command::Publish(ticket)))
    );
    assert_eq!(loads.lookup(19).unwrap().owner, 3);
    let detach = loads.cancel(ticket).unwrap();
    mailbox.begin(detach, &loads, true).unwrap();
    transfer(&mut mailbox);
    let reply = response(&mailbox, 0, 2);
    assert_eq!(mailbox.reply(&reply, &mut loads), Ok(None));
    assert_eq!(loads.capacity().rows_used, 3);
    transfer(&mut mailbox);
    let reply = response(&mailbox, 0, 2);
    assert_eq!(
        mailbox.reply(&reply, &mut loads),
        Ok(Some(Command::Release(ticket)))
    );
    assert_eq!(loads.capacity().rows_used, 1);
    assert_eq!(loads.cancel(other), Ok(Command::Release(other)));
}

#[test]
fn malformed_mismatched_and_bootstrap_replies_cannot_publish() {
    let mut loads = Loads::new(16);
    let ticket = reserve(&mut loads, 0, &[17]);
    let mut mailbox = Mailbox::new();
    let command = initialize(&mut loads, ticket);
    mailbox.begin(command, &loads, true).unwrap();
    let reply = response(&mailbox, 0, 1);
    assert_eq!(mailbox.reply(&reply, &mut loads), Err(Error::WrongTransfer));
    transfer(&mut mailbox);
    for index in [0, 4, 5, 6, 7, 8, 9, 15] {
        let mut bad = reply;
        bad[index] ^= 1;
        assert_eq!(mailbox.reply(&bad, &mut loads), Err(Error::InvalidReply));
        if (5..10).contains(&index) {
            checksum(&mut bad);
            assert_eq!(mailbox.reply(&bad, &mut loads), Err(Error::MismatchedReply));
        }
    }
    let unsupported = response(&mailbox, 1, 0);
    assert_eq!(
        mailbox.reply(&unsupported, &mut loads),
        Err(Error::Rejected(1))
    );
    let incapable = response(&mailbox, 0, 0);
    assert_eq!(
        mailbox.reply(&incapable, &mut loads),
        Err(Error::MissingCapability)
    );
    assert_eq!(loads.lookup(17), None);
    assert_eq!(loads.capacity().rows_used, 1);
}

#[test]
fn partial_failure_quarantines_all_rows_and_requires_drained_detach() {
    let mut loads = Loads::new(4);
    let ticket = reserve(&mut loads, 0, &[17, 18]);
    let mut mailbox = Mailbox::new();
    let command = initialize(&mut loads, ticket);
    assert_eq!(
        mailbox.begin(command, &loads, false),
        Err(Error::NotDrained)
    );
    mailbox.begin(command, &loads, true).unwrap();
    assert_eq!(mailbox.begin(command, &loads, true), Err(Error::Busy));
    transfer(&mut mailbox);
    let reply = response(&mailbox, 0, 1);
    mailbox.reply(&reply, &mut loads).unwrap();
    transfer(&mut mailbox);
    let late = response(&mailbox, 0, 1);
    assert_eq!(
        mailbox.abandon_after_drain(&mut loads, false),
        Err(Error::NotDrained)
    );
    let detach = mailbox.abandon_after_drain(&mut loads, true).unwrap();
    assert_eq!(loads.lookup(17), None);
    assert_eq!(loads.capacity().rows_used, 2);
    assert_eq!(
        loads.reserve(
            1,
            &[Request {
                type_no: 19,
                tracks: 1
            }]
        ),
        Err(LoadError::Capacity)
    );
    mailbox.begin(detach, &loads, true).unwrap();
    transfer(&mut mailbox);
    assert_eq!(
        mailbox.reply(&late, &mut loads),
        Err(Error::MismatchedReply)
    );
    let unsupported = response(&mailbox, 1, 0);
    assert_eq!(
        mailbox.reply(&unsupported, &mut loads),
        Err(Error::Rejected(1))
    );
    assert_eq!(loads.capacity().rows_used, 2);
}

#[test]
fn sequence_exhaustion_reserves_the_whole_batch_and_never_wraps() {
    let mut loads = Loads::new(16);
    let ticket = reserve(&mut loads, 31, &[17, 18]);
    let command = initialize(&mut loads, ticket);
    let mut mailbox = Mailbox::new();
    mailbox.sequence = u32::MAX - 1;
    assert_eq!(
        mailbox.begin(command, &loads, true),
        Err(Error::SequenceExhausted)
    );
    assert_eq!(mailbox.write(), None);
    assert_eq!(loads.lookup(17), None);
    assert_eq!(
        loads.reserve(
            32,
            &[Request {
                type_no: 19,
                tracks: 1
            }]
        ),
        Err(LoadError::Invalid)
    );
}

#[test]
fn cancellation_between_row_acks_cannot_publish_or_schedule_another_row() {
    let mut loads = Loads::new(16);
    let ticket = reserve(&mut loads, 0, &[17, 18]);
    let mut mailbox = Mailbox::new();
    assert_eq!(
        mailbox.begin(Command::Initialize(ticket), &loads, true),
        Err(Error::Load(LoadError::WrongPhase))
    );
    let command = initialize(&mut loads, ticket);
    mailbox.begin(command, &loads, true).unwrap();
    transfer(&mut mailbox);
    let reply = response(&mailbox, 0, 1);
    loads.cancel(ticket).unwrap();
    assert_eq!(
        mailbox.reply(&reply, &mut loads),
        Err(Error::Load(LoadError::WrongPhase))
    );
    assert_eq!(loads.lookup(17), None);
    assert_eq!(loads.capacity().rows_used, 2);
    assert_eq!(mailbox.write(), None);
}

#[test]
fn instrument_memory_waits_for_all_mailbox_detach_rows_before_freeing() {
    use crate::instrument_load::{Ledger, MemoryError, MemoryRequest};
    let mut loads = Loads::new(16);
    let ticket = reserve(&mut loads, 0, &[20, 21]);
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 128,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    loads
        .reserve_memory(
            ticket,
            &mut ledger,
            &MemoryRequest {
                image_bytes: 32,
                bss_bytes: 0,
                state_bytes: 64,
                align: 8,
            },
        )
        .unwrap();
    let mut mailbox = Mailbox::new();
    let command = initialize(&mut loads, ticket);
    mailbox.begin(command, &loads, true).unwrap();
    for row in 0..2 {
        transfer(&mut mailbox);
        let reply = response(&mailbox, 0, 1);
        let result = mailbox.reply(&reply, &mut loads).unwrap();
        assert_eq!(
            result,
            if row == 1 {
                Some(Command::Publish(ticket))
            } else {
                None
            }
        );
    }
    let detach = loads.cancel(ticket).unwrap();
    mailbox.begin(detach, &loads, true).unwrap();
    transfer(&mut mailbox);
    let first = response(&mailbox, 0, 2);
    assert_eq!(mailbox.reply(&first, &mut loads), Ok(None));
    assert_eq!(
        loads.release_memory(ticket, &mut ledger),
        Err(MemoryError::Load(
            crate::instrument_registry::Error::WrongPhase
        ))
    );
    assert_eq!(ledger.lengths[0], 128);
    transfer(&mut mailbox);
    assert_eq!(
        mailbox.reply(&first, &mut loads),
        Err(Error::MismatchedReply)
    );
    let last = response(&mailbox, 0, 2);
    assert_eq!(
        mailbox.reply(&last, &mut loads),
        Ok(Some(Command::Release(ticket)))
    );
    assert_eq!(ledger.lengths[0], 128);
    loads.release_memory(ticket, &mut ledger).unwrap();
    assert_eq!(ledger.lengths[0], 0);
}

#[test]
fn instrument_install_binds_plan_and_waits_for_every_dsp_reply() {
    run_install_scenario(None);
}

#[test]
fn instrument_partial_install_failure_releases_only_after_all_detach_replies() {
    run_install_scenario(Some(0));
    run_install_scenario(Some(1));
}

fn run_install_scenario(cancel_after: Option<u32>) {
    use crate::instrument_admission::{prepare, Limits, Range, Spec};
    use crate::instrument_image::Transfer;
    use crate::instrument_load::{Ledger, MemoryRequest};
    let mut loads = Loads::new(32);
    let t = reserve(&mut loads, 3, &[18, 19]);
    let mut offsets = [0; 4];
    let mut lengths = [0; 4];
    let grant = loads
        .reserve_memory(
            t,
            &mut Ledger {
                base: 0x20010000,
                bytes: 4096,
                offsets: &mut offsets,
                lengths: &mut lengths,
            },
            &MemoryRequest {
                image_bytes: 256,
                bss_bytes: 8,
                state_bytes: 64,
                align: 8,
            },
        )
        .unwrap();
    let spec = Spec {
        type_no: 18,
        state_base: grant.state.start,
        state_bytes: 12,
        text: grant.image,
        process_text: grant.image,
        reset_sw: 0xb88000,
        process_sw: 0xb88003,
        reset_instructions: 100,
        process_instructions: 100,
        call_depth: 2,
        stack_bytes: 64,
        max_events: 8,
        max_controls: 32,
    };
    let plan = prepare(
        &loads,
        t,
        &grant,
        &[
            spec,
            Spec {
                type_no: 19,
                state_base: grant.state.start + 32,
                ..spec
            },
        ],
        &Limits {
            l2: Range {
                start: 0x20000000,
                end: 0x20080000,
            },
            reset_frame: 1000,
            reset_overhead: 500,
            process_total: 1000,
            call_depth: 4,
            stack_bytes: 128,
        },
    )
    .unwrap();
    let bytes = [0x5a; 256];
    let mut image = Transfer::begin(&mut loads, t, &grant, &bytes, true).unwrap();
    let mut mailbox = Mailbox::new();
    assert_eq!(
        mailbox.begin_install(&image, &plan, &loads, true),
        Err(Error::Image(crate::instrument_image::Error::NotReady))
    );
    while let Some(write) = image.next(&loads).unwrap() {
        image.acknowledged(&loads, write.ack).unwrap();
    }
    assert_eq!(
        mailbox.begin_install(&image, &plan, &loads, false),
        Err(Error::NotDrained)
    );
    mailbox.begin_install(&image, &plan, &loads, true).unwrap();
    let words = transfer(&mut mailbox);
    assert_eq!(words[5], 4);
    assert_eq!(words[7..15], plan.rows()[0].unwrap().words[3..11]);
    assert!(mailbox.take_installed().is_none()); // transfer ACK alone cannot authorize init
    if cancel_after == Some(0) {
        cleanup_partial_install(&mut mailbox, &mut loads, t, &mut offsets, &mut lengths);
        return;
    }
    let missing_cap = response(&mailbox, 0, 3);
    assert_eq!(
        mailbox.reply(&missing_cap, &mut loads),
        Err(Error::MissingCapability)
    );
    let rejected = response(&mailbox, 3, 7);
    assert_eq!(
        mailbox.reply(&rejected, &mut loads),
        Err(Error::Rejected(3))
    );
    let first = response(&mailbox, 0, 7);
    assert_eq!(mailbox.reply(&first, &mut loads), Ok(None));
    assert!(mailbox.take_installed().is_none());
    let process = transfer(&mut mailbox);
    assert_eq!(process[5], 6);
    assert_eq!(process[7..15], [18, spec.process_sw, 0xb88000, 0xb88080, 8, 32, 0, 0]);
    let old_capabilities = response(&mailbox, 0, 7);
    assert_eq!(mailbox.reply(&old_capabilities, &mut loads), Err(Error::MissingCapability));
    assert!(mailbox.take_installed().is_none());
    let process_reply = response(&mailbox, 0, 15);
    assert_eq!(mailbox.reply(&process_reply, &mut loads), Ok(None));
    let words = transfer(&mut mailbox);
    assert_eq!(words[7..15], plan.rows()[1].unwrap().words[3..11]);
    assert_eq!(
        mailbox.reply(&first, &mut loads),
        Err(Error::MismatchedReply)
    );
    if cancel_after == Some(1) {
        let rejected = response(&mailbox, 3, 7);
        assert_eq!(
            mailbox.reply(&rejected, &mut loads),
            Err(Error::Rejected(3))
        );
        cleanup_partial_install(&mut mailbox, &mut loads, t, &mut offsets, &mut lengths);
        return;
    }
    let second = response(&mailbox, 0, 7);
    mailbox.reply(&second, &mut loads).unwrap();
    assert!(mailbox.take_installed().is_none());
    let process = transfer(&mut mailbox);
    assert_eq!(process[5], 6);
    assert_eq!(process[7], 19);
    let process_reply = response(&mailbox, 0, 15);
    mailbox.reply(&process_reply, &mut loads).unwrap();
    assert!(loads.lookup(18).is_none());
    let receipt = mailbox.take_installed().unwrap();
    assert!(mailbox.take_installed().is_none());
    assert_eq!(
        image.finish(&mut loads, &receipt),
        Ok(Command::Initialize(t))
    );
    mailbox.begin(Command::Initialize(t), &loads, true).unwrap();
    for _ in 0..2 {
        transfer(&mut mailbox);
        let reply = response(&mailbox, 0, 7);
        mailbox.reply(&reply, &mut loads).unwrap();
    }
    assert_eq!(loads.lookup(18).unwrap().owner, 3);
}

fn cleanup_partial_install(
    mailbox: &mut Mailbox,
    loads: &mut Loads,
    ticket: Ticket,
    offsets: &mut [u32],
    lengths: &mut [u32],
) {
    use crate::instrument_load::Ledger;
    assert!(mailbox.take_installed().is_none());
    assert_eq!(
        mailbox.abandon_after_drain(loads, false),
        Err(Error::NotDrained)
    );
    let old_reply = response(mailbox, 0, 7);
    let detach = mailbox.abandon_after_drain(loads, true).unwrap();
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 4096,
        offsets,
        lengths,
    };
    let retained = ledger.lengths[3];
    assert!(retained > 0);
    assert!(loads.release_memory(ticket, &mut ledger).is_err());
    mailbox.begin(detach, loads, true).unwrap();
    for index in 0..2 {
        transfer(mailbox);
        assert_eq!(
            mailbox.reply(&old_reply, loads),
            Err(Error::MismatchedReply)
        );
        assert!(loads.release_memory(ticket, &mut ledger).is_err());
        let ack = response(mailbox, 0, 7);
        assert_eq!(
            mailbox.reply(&ack, loads).unwrap(),
            if index == 1 {
                Some(Command::Release(ticket))
            } else {
                None
            }
        );
        assert_eq!(ledger.lengths[3], retained);
    }
    loads.release_memory(ticket, &mut ledger).unwrap();
    assert_eq!(ledger.lengths[3], 0);
    let next = reserve(loads, 3, &[18, 19]);
    assert!(next.generation() > ticket.generation());
}
