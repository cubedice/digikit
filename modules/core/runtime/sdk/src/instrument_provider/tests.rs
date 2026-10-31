use super::*;
use crate::{instrument_registry::Request, instrument_transport::Status};

unsafe extern "C" fn send(_: u32, _: u32, _: *const u8, _: u32, _: *mut u32) -> i32 {
    panic!("binding must not call send")
}
unsafe extern "C" fn link_poll(_: u32, _: *mut Status) -> i32 {
    panic!("reply ingestion must not poll link")
}
unsafe extern "C" fn absent(_: *mut u32, _: u32) -> i32 {
    0
}

fn replies(snapshot: Snapshot) -> Replies {
    unsafe { Providers::bind(Some(send), Some(link_poll), Some(snapshot)) }
        .unwrap()
        .into_parts()
        .1
}
fn pending() -> (Loads, Mailbox, Command) {
    let mut loads = Loads::new(16);
    let ticket = loads
        .reserve(
            0,
            &[Request {
                type_no: 17,
                tracks: 2,
            }],
        )
        .unwrap();
    loads.start(ticket).unwrap();
    let initialize = loads.image_acked(ticket).unwrap();
    let mut mailbox = Mailbox::new();
    mailbox.begin(initialize, &loads, true).unwrap();
    (loads, mailbox, Command::Publish(ticket))
}
fn drain(mailbox: &mut Mailbox) {
    while let Some(write) = mailbox.write() {
        mailbox.write_acked(write.sequence, write.stage).unwrap();
    }
}
unsafe fn copy_reply(
    out: *mut u32,
    capacity: u32,
    seq: u32,
    status: u32,
    caps: u32,
    reserved: u32,
) -> i32 {
    if capacity != 16 {
        return -1;
    }
    // First ticket/row in a fresh boot: owner 0, generation 1, sequence 1.
    let mut words = [
        0x494ea101, 99, 0, 42, 0x49525231, seq, 0, 1, 2, 0, status, caps, reserved, 0, 0, 0,
    ];
    let (mut a, mut b) = (0u32, 0u32);
    for word in &words[..15] {
        a = a.wrapping_add(*word);
        b = b.wrapping_add(a);
    }
    words[15] = a ^ b;
    core::ptr::copy_nonoverlapping(words.as_ptr(), out, 16);
    16
}
unsafe extern "C" fn good(out: *mut u32, n: u32) -> i32 {
    copy_reply(out, n, 1, 0, 1, 0)
}
unsafe extern "C" fn stale(out: *mut u32, n: u32) -> i32 {
    copy_reply(out, n, 2, 0, 1, 0)
}
unsafe extern "C" fn bootstrap(out: *mut u32, n: u32) -> i32 {
    copy_reply(out, n, 1, 1, 0, 0)
}
unsafe extern "C" fn incapable(out: *mut u32, n: u32) -> i32 {
    copy_reply(out, n, 1, 0, 0, 0)
}
unsafe extern "C" fn reserved(out: *mut u32, n: u32) -> i32 {
    copy_reply(out, n, 1, 0, 1, 1)
}
unsafe extern "C" fn short(out: *mut u32, n: u32) -> i32 {
    good(out, n);
    15
}
unsafe extern "C" fn failed(_: *mut u32, _: u32) -> i32 {
    -1
}

#[test]
fn instrument_provider_requires_all_optional_entries_without_calling_them() {
    for bits in 0..8 {
        let provider = unsafe {
            Providers::bind(
                if bits & 1 != 0 { Some(send) } else { None },
                if bits & 2 != 0 { Some(link_poll) } else { None },
                if bits & 4 != 0 { Some(good) } else { None },
            )
        };
        assert_eq!(provider.is_some(), bits == 7);
    }
}
#[test]
fn instrument_provider_waits_for_exact_reply_after_drained_writes() {
    let (mut loads, mut mailbox, expected) = pending();
    assert_eq!(
        replies(good).poll(&mut mailbox, &mut loads),
        Err(Error::Mailbox(instrument_mailbox::Error::WrongTransfer))
    );
    assert_eq!(loads.lookup(17), None);
    drain(&mut mailbox);
    for source in [absent as Snapshot, stale] {
        assert_eq!(
            replies(source).poll(&mut mailbox, &mut loads),
            Ok(Progress::Waiting)
        );
        assert_eq!(loads.lookup(17), None);
    }
    assert_eq!(
        replies(good).poll(&mut mailbox, &mut loads),
        Ok(Progress::Complete(expected))
    );
    assert!(loads.lookup(17).is_some());
    assert_eq!(
        replies(good).poll(&mut mailbox, &mut loads),
        Ok(Progress::Waiting)
    );
}
#[test]
fn instrument_provider_rejects_partial_or_failed_copies_without_advancing() {
    let (mut loads, mut mailbox, expected) = pending();
    drain(&mut mailbox);
    for (source, result) in [(short as Snapshot, 15), (failed, -1)] {
        assert_eq!(
            replies(source).poll(&mut mailbox, &mut loads),
            Err(Error::Provider(result))
        );
        assert_eq!(loads.lookup(17), None);
    }
    assert_eq!(
        replies(good).poll(&mut mailbox, &mut loads),
        Ok(Progress::Complete(expected))
    );
}
#[test]
fn instrument_provider_rejects_bootstrap_missing_capability_and_reserved_payload() {
    let (mut loads, mut mailbox, expected) = pending();
    drain(&mut mailbox);
    for (source, error) in [
        (
            bootstrap as Snapshot,
            instrument_mailbox::Error::Rejected(1),
        ),
        (incapable, instrument_mailbox::Error::MissingCapability),
        (reserved, instrument_mailbox::Error::InvalidReply),
    ] {
        assert_eq!(
            replies(source).poll(&mut mailbox, &mut loads),
            Err(Error::Mailbox(error))
        );
        assert_eq!(loads.lookup(17), None);
        assert_eq!(loads.capacity().rows_used, 1);
    }
    assert_eq!(
        replies(good).poll(&mut mailbox, &mut loads),
        Ok(Progress::Complete(expected))
    );
}

#[test]
fn instrument_provider_composes_tagged_write_drainage_with_registry_reply() {
    extern crate std;
    use crate::instrument_transport::{Driver, Progress as LinkProgress, Started, Transport};
    struct Link {
        id: u32,
        pending: bool,
        polls: u32,
    }
    unsafe impl Transport for Link {
        unsafe fn start(
            &mut self,
            window: u32,
            destination: u32,
            source: *const u8,
            bytes: u32,
        ) -> (i32, u32) {
            assert!(!self.pending);
            assert_eq!(window, 3);
            let data = core::slice::from_raw_parts(source, bytes as usize);
            match self.id {
                0 => {
                    assert_eq!(destination, 0);
                    assert_eq!(data, [0; 4]);
                }
                1 => {
                    assert_eq!(destination, 1);
                    assert_eq!(bytes, 60);
                    assert_eq!(&data[..4], &1u32.to_le_bytes());
                }
                2 => {
                    assert_eq!(destination, 0);
                    assert_eq!(data, 0x49524731u32.to_le_bytes());
                }
                _ => panic!("unexpected write"),
            }
            self.id += 1;
            self.pending = true;
            self.polls = 0;
            (0, self.id)
        }
        fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
            assert_eq!(id, self.id);
            assert!(self.pending);
            status.state = 2;
            self.polls += 1;
            if self.polls == 1 {
                return 0;
            } // terminal but END frames not drained
            self.pending = false;
            1
        }
    }
    let (mut loads, mut mailbox, expected) = pending();
    let mut driver = Driver::new(
        Link {
            id: 0,
            pending: false,
            polls: 0,
        },
        std::boxed::Box::leak(std::boxed::Box::new([0; 256])),
    );
    let mut reply = replies(good);
    for _ in 0..3 {
        let write = mailbox.write().unwrap();
        assert_eq!(driver.start_mailbox(&write), Ok(Started::Accepted));
        assert_eq!(driver.poll(), Ok(LinkProgress::Pending));
        assert_eq!(mailbox.write(), Some(write));
        assert_eq!(
            reply.poll(&mut mailbox, &mut loads),
            Err(Error::Mailbox(instrument_mailbox::Error::WrongTransfer))
        );
        assert_eq!(loads.lookup(17), None);
        match driver.poll().unwrap() {
            LinkProgress::Complete(completed) => completed.mailbox(&mut mailbox).unwrap(),
            _ => panic!("missing drained completion"),
        }
    }
    assert!(mailbox.write().is_none());
    assert_eq!(loads.lookup(17), None); // transport completion cannot publish
    assert_eq!(
        reply.poll(&mut mailbox, &mut loads),
        Ok(Progress::Complete(expected))
    );
    assert!(loads.lookup(17).is_some());
}

#[test]
fn boot_probe_does_not_consume_or_publish_a_pending_reply() {
    let (mut loads, mut mailbox, expected) = pending();
    drain(&mut mailbox);
    let mut reply = replies(good);
    assert_eq!(reply.check_boot(), Ok(()));
    assert_eq!(loads.lookup(17), None);
    assert_eq!(
        reply.poll(&mut mailbox, &mut loads),
        Ok(Progress::Complete(expected))
    );
    assert!(loads.lookup(17).is_some());
    assert_eq!(replies(absent).check_boot(), Ok(()));
    assert_eq!(replies(short).check_boot(), Err(Error::Provider(15)));
}
