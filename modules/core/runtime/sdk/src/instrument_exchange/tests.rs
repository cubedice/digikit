extern crate std;
use super::*;
use crate::{
    instrument_provider::Providers,
    instrument_registry::{Request, Ticket},
    instrument_transport::Status,
};
use std::{boxed::Box, cell::Cell, rc::Rc};

#[derive(Default)]
struct State {
    starts: Cell<u32>,
    polls: Cell<u32>,
    mode: Cell<u32>,
    busy: Cell<bool>,
    source: Cell<*const u8>,
}
struct Link(Rc<State>);
unsafe impl Transport for Link {
    unsafe fn start(&mut self, _: u32, _: u32, source: *const u8, _: u32) -> (i32, u32) {
        if self.0.busy.get() {
            return (-1, 0);
        }
        let id = self.0.starts.get() + 1;
        self.0.starts.set(id);
        self.0.source.set(source);
        (0, id)
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        assert_eq!(id, self.0.starts.get());
        self.0.polls.set(self.0.polls.get() + 1);
        match self.0.mode.get() {
            0 => {
                status.state = 2;
                0
            } // terminal, END still outstanding
            1 => {
                status.state = 2;
                1
            }
            2 => {
                status.state = 3;
                status.err = 9;
                1
            }
            _ => -1,
        }
    }
}
unsafe extern "C" fn unused_send(_: u32, _: u32, _: *const u8, _: u32, _: *mut u32) -> i32 {
    -2
}
unsafe extern "C" fn unused_poll(_: u32, _: *mut Status) -> i32 {
    -1
}
unsafe extern "C" fn absent(_: *mut u32, _: u32) -> i32 {
    0
}
unsafe extern "C" fn good(out: *mut u32, n: u32) -> i32 {
    assert_eq!(n, 16);
    let mut r = [
        0x494ea101u32,
        99,
        0,
        42,
        0x49525231,
        1,
        0,
        1,
        2,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
    ];
    let (mut a, mut b) = (0u32, 0u32);
    for w in &r[..15] {
        a = a.wrapping_add(*w);
        b = b.wrapping_add(a);
    }
    r[15] = a ^ b;
    core::ptr::copy_nonoverlapping(r.as_ptr(), out, 16);
    16
}
struct Fixture {
    driver: Driver<Link>,
    state: Rc<State>,
    mailbox: Mailbox,
    loads: Loads,
    replies: Replies,
    ticket: Ticket,
}
impl Fixture {
    fn new(snapshot: crate::instrument_provider::Snapshot) -> Self {
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
        let command = loads.image_acked(ticket).unwrap();
        let mut mailbox = Mailbox::new();
        mailbox.begin(command, &loads, true).unwrap();
        let state = Rc::new(State::default());
        state.mode.set(1);
        let replies =
            unsafe { Providers::bind(Some(unused_send), Some(unused_poll), Some(snapshot)) }
                .unwrap()
                .into_parts()
                .1;
        Self {
            driver: Driver::new(Link(state.clone()), Box::leak(Box::new([0; 256]))),
            state,
            mailbox,
            loads,
            replies,
            ticket,
        }
    }
    fn exchange(&mut self, start: u32, budget: u32) -> Exchange<'_, Link> {
        Exchange::new(
            &mut self.driver,
            &mut self.mailbox,
            &mut self.replies,
            &mut self.loads,
            start,
            budget,
        )
        .unwrap()
    }
}
#[test]
fn instrument_exchange_publishes_only_after_drained_writes_and_reply() {
    let mut f = Fixture::new(good);
    let expected = Command::Publish(f.ticket);
    let mut e = f.exchange(0, 20);
    for now in 0..6 {
        assert_eq!(e.tick(now), Progress::Pending);
    }
    assert_eq!(e.loads.lookup(17), None);
    assert_eq!(e.tick(6), Progress::Complete(Some(expected)));
    assert_eq!(e.tick(7), Progress::Finished);
    drop(e);
    assert_eq!(f.state.starts.get(), 3);
    assert!(f.loads.lookup(17).is_some());
}
#[test]
fn instrument_exchange_timeout_drains_without_ack_before_fresh_detach() {
    let mut f = Fixture::new(absent);
    f.state.mode.set(0);
    let state = f.state.clone();
    let ticket = f.ticket;
    let mut e = f.exchange(0, 5);
    assert_eq!(e.tick(0), Progress::Pending);
    assert_eq!(e.tick(5), Progress::Pending);
    assert_eq!(e.tick(6), Progress::Pending);
    assert_eq!(state.starts.get(), 1);
    state.mode.set(1);
    assert_eq!(
        e.tick(7),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(ticket)
        }
    );
    assert_eq!(e.tick(8), Progress::Finished);
    drop(e);
    assert_eq!(f.loads.capacity().rows_used, 1);
    assert_eq!(f.loads.lookup(17), None);
    f.mailbox
        .begin(Command::Detach(ticket), &f.loads, true)
        .unwrap();
    assert_eq!(f.mailbox.write().unwrap().sequence, 2);
}
#[test]
fn instrument_exchange_stuck_drain_blocks_and_preserves_source() {
    let mut f = Fixture::new(absent);
    f.state.mode.set(0);
    let state = f.state.clone();
    let mut e = f.exchange(0, 4);
    e.tick(0);
    assert_eq!(e.tick(4), Progress::Pending);
    assert_eq!(e.tick(8), Progress::Blocked(Failure::Timeout));
    let polls = state.polls.get();
    assert_eq!(e.tick(9), Progress::Blocked(Failure::Timeout));
    assert_eq!(state.polls.get(), polls);
    drop(e);
    assert_eq!(f.loads.capacity().rows_used, 1);
    assert_eq!(state.starts.get(), 1);
    assert_eq!(
        unsafe { core::slice::from_raw_parts(state.source.get(), 4) },
        [0; 4]
    );
}
#[test]
fn instrument_exchange_busy_and_missing_replies_cannot_extend_deadline() {
    for busy in [true, false] {
        let mut f = Fixture::new(absent);
        f.state.busy.set(busy);
        let ticket = f.ticket;
        let mut e = f.exchange(0, 8);
        for now in 0..8 {
            assert_eq!(e.tick(now), Progress::Pending);
        }
        assert_eq!(
            e.tick(8),
            Progress::Recover {
                cause: Failure::Timeout,
                command: Command::Detach(ticket)
            }
        );
        assert_eq!(e.loads.capacity().rows_used, 1);
    }
}
#[test]
fn instrument_exchange_deadline_wraps_and_late_success_does_not_publish() {
    let mut f = Fixture::new(good);
    let ticket = f.ticket;
    let start = u32::MAX - 3;
    let mut e = f.exchange(start, 6);
    for i in 0..6 {
        assert_eq!(e.tick(start.wrapping_add(i)), Progress::Pending);
    }
    assert_eq!(
        e.tick(start.wrapping_add(6)),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(ticket)
        }
    );
    assert_eq!(e.loads.lookup(17), None);
}
#[test]
fn instrument_exchange_link_errors_distinguish_drained_from_uncertain() {
    for mode in [2, 3] {
        let mut f = Fixture::new(absent);
        f.state.mode.set(mode);
        let ticket = f.ticket;
        let mut e = f.exchange(0, 8);
        e.tick(0);
        let expected = if mode == 2 {
            Progress::Recover {
                cause: Failure::Link,
                command: Command::Detach(ticket),
            }
        } else {
            Progress::Blocked(Failure::Link)
        };
        assert_eq!(e.tick(1), expected);
        assert_eq!(e.loads.capacity().rows_used, 1);
        assert_eq!(e.loads.lookup(17), None);
    }
}
#[test]
fn instrument_exchange_rejects_invalid_deadline_and_idle_mailbox() {
    let mut f = Fixture::new(absent);
    for budget in [0, 0x80000000, u32::MAX] {
        assert!(Exchange::new(
            &mut f.driver,
            &mut f.mailbox,
            &mut f.replies,
            &mut f.loads,
            0,
            budget
        )
        .is_none());
    }
    let mut idle = Mailbox::new();
    assert!(Exchange::new(
        &mut f.driver,
        &mut idle,
        &mut f.replies,
        &mut f.loads,
        0,
        10
    )
    .is_none());
    assert_eq!(f.state.starts.get(), 0);
}

unsafe extern "C" fn detached(out: *mut u32, n: u32) -> i32 {
    good(out, n);
    let r = core::slice::from_raw_parts_mut(out, 16);
    r[5] = 2;
    r[8] = 3;
    r[11] = 2;
    let (mut a, mut b) = (0u32, 0u32);
    for w in &r[..15] {
        a = a.wrapping_add(*w);
        b = b.wrapping_add(a);
    }
    r[15] = a ^ b;
    16
}
#[test]
fn instrument_exchange_recovery_releases_only_after_fresh_detach_reply() {
    let mut f = Fixture::new(detached);
    let ticket = f.ticket;
    let mut e = f.exchange(0, 8);
    for now in 0..8 {
        assert_eq!(e.tick(now), Progress::Pending);
    } // reply is for future detach
    assert_eq!(
        e.tick(8),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(ticket)
        }
    );
    drop(e);
    assert_eq!(f.loads.capacity().rows_used, 1);
    f.mailbox
        .begin(Command::Detach(ticket), &f.loads, true)
        .unwrap();
    let mut recovery = f.exchange(9, 20);
    for now in 9..15 {
        assert_eq!(recovery.tick(now), Progress::Pending);
    }
    assert_eq!(recovery.loads.capacity().rows_used, 1);
    assert_eq!(
        recovery.tick(15),
        Progress::Complete(Some(Command::Release(ticket)))
    );
    assert_eq!(recovery.loads.capacity().rows_used, 0);
}
unsafe extern "C" fn bad_copy(_: *mut u32, _: u32) -> i32 {
    15
}
#[test]
fn instrument_exchange_invalid_reply_recovers_without_freeing_rows() {
    let mut f = Fixture::new(bad_copy);
    let ticket = f.ticket;
    let mut e = f.exchange(0, 20);
    for now in 0..6 {
        assert_eq!(e.tick(now), Progress::Pending);
    }
    assert_eq!(
        e.tick(6),
        Progress::Recover {
            cause: Failure::Reply,
            command: Command::Detach(ticket)
        }
    );
    assert_eq!(e.loads.capacity().rows_used, 1);
}

#[test]
fn instrument_exchange_boot_invalidation_stops_link_and_cached_reply() {
    let mut f = Fixture::new(good);
    let state = f.state.clone();
    let mut e = f.exchange(0, 20);
    for now in 0..6 {
        assert_eq!(e.tick(now), Progress::Pending);
    }
    let polls = state.polls.get();
    e.invalidate_boot();
    assert_eq!(e.tick(6), Progress::Blocked(Failure::Boot));
    drop(e);
    assert_eq!(
        f.driver.poll(),
        Err(crate::instrument_transport::Error::Uncertain)
    );
    assert_eq!(
        f.replies.poll(&mut f.mailbox, &mut f.loads),
        Err(crate::instrument_provider::Error::BootInvalidated)
    );
    assert_eq!(state.polls.get(), polls);
    assert_eq!(f.loads.lookup(17), None);
    assert_eq!(f.loads.capacity().rows_used, 1);
}

unsafe extern "C" fn lost_boot(_: *mut u32, _: u32) -> i32 {
    -2
}
#[test]
fn instrument_exchange_provider_boot_loss_invalidates_link_without_detach() {
    let mut f = Fixture::new(lost_boot);
    let mut e = f.exchange(0, 20);
    for now in 0..6 {
        assert_eq!(e.tick(now), Progress::Pending);
    }
    assert_eq!(e.tick(6), Progress::Blocked(Failure::Boot));
    drop(e);
    assert_eq!(
        f.driver.poll(),
        Err(crate::instrument_transport::Error::Uncertain)
    );
    assert_eq!(
        f.replies.poll(&mut f.mailbox, &mut f.loads),
        Err(crate::instrument_provider::Error::BootInvalidated)
    );
    assert_eq!(f.loads.capacity().rows_used, 1);
    assert_eq!(f.loads.lookup(17), None);
    assert!(f.mailbox.has_pending()); // no blind detach into an unknown boot
}
