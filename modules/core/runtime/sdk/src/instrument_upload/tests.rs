extern crate std;
use super::*;
use crate::{
    instrument_admission::Grant,
    instrument_load::{Ledger, MemoryRequest},
    instrument_registry::{Request, Ticket},
    instrument_transport::Status,
};
use std::{
    boxed::Box,
    cell::{Cell, RefCell},
    rc::Rc,
    vec::Vec,
};
#[derive(Default)]
struct State {
    mode: Cell<u32>,
    busy: Cell<bool>,
    starts: Cell<u32>,
    polls: Cell<u32>,
    bytes: RefCell<Vec<u8>>,
}
struct Link(Rc<State>);
unsafe impl Transport for Link {
    unsafe fn start(&mut self, win: u32, dst: u32, src: *const u8, n: u32) -> (i32, u32) {
        if self.0.busy.get() {
            return (-1, 0);
        }
        assert_eq!(win, 0);
        let mut bytes = self.0.bytes.borrow_mut();
        assert_eq!(dst, 0x20010000 + bytes.len() as u32);
        bytes.extend_from_slice(core::slice::from_raw_parts(src, n as usize));
        let id = self.0.starts.get() + 1;
        self.0.starts.set(id);
        (0, id)
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        assert_eq!(id, self.0.starts.get());
        self.0.polls.set(self.0.polls.get() + 1);
        match self.0.mode.get() {
            0 => {
                status.state = 2;
                0
            }
            1 => {
                status.state = 2;
                1
            }
            2 => {
                status.state = 3;
                1
            }
            _ => -1,
        }
    }
}
struct Fixture {
    loads: Loads,
    ticket: Ticket,
    grant: Grant,
    offsets: [u32; 2],
    lengths: [u32; 2],
    state: Rc<State>,
    driver: Driver<Link>,
}
impl Fixture {
    fn new() -> Self {
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
        let (mut offsets, mut lengths) = ([0; 2], [0; 2]);
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
                    image_bytes: 300,
                    bss_bytes: 300,
                    state_bytes: 32,
                    align: 16,
                },
            )
            .unwrap();
        let state = Rc::new(State::default());
        state.mode.set(1);
        let driver = Driver::new(Link(state.clone()), Box::leak(Box::new([0; 256])));
        Self {
            loads,
            ticket,
            grant,
            offsets,
            lengths,
            state,
            driver,
        }
    }
    fn assert_retained(&mut self) {
        let before = self.lengths;
        assert!(self
            .loads
            .release_memory(
                self.ticket,
                &mut Ledger {
                    base: 0x20010000,
                    bytes: 1024,
                    offsets: &mut self.offsets,
                    lengths: &mut self.lengths
                }
            )
            .is_err());
        assert_eq!(self.lengths, before);
        assert!(self.lengths[0] > 0);
        assert_eq!(self.loads.lookup(20), None);
    }
}
const IMAGE: [u8; 300] = [0x5a; 300];
#[test]
fn instrument_upload_chunks_image_and_zero_fill_but_does_not_initialize() {
    let mut f = Fixture::new();
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 30).unwrap();
    for now in 0..8 {
        assert_eq!(upload.tick(now), Progress::Pending);
    }
    assert_eq!(upload.tick(8), Progress::ReadyToInstall);
    assert_eq!(upload.tick(9), Progress::Finished);
    drop(upload);
    assert!(transfer.install_ticket(&f.loads).is_ok());
    let bytes = f.state.bytes.borrow();
    assert_eq!(&bytes[..300], &IMAGE);
    assert_eq!(
        bytes.len(),
        (f.grant.state.start - f.grant.image.start) as usize
    );
    assert!(bytes[300..].iter().all(|b| *b == 0));
    drop(bytes);
    f.assert_retained();
}
#[test]
fn instrument_upload_timeout_discards_late_ack_and_retains_allocation() {
    let mut f = Fixture::new();
    f.state.mode.set(0);
    let state = f.state.clone();
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 4).unwrap();
    assert_eq!(upload.tick(0), Progress::Pending);
    assert_eq!(upload.tick(4), Progress::Pending);
    state.mode.set(1);
    assert_eq!(
        upload.tick(5),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(f.ticket)
        }
    );
    drop(upload);
    assert_eq!(state.starts.get(), 1);
    assert_eq!(
        transfer.next(&f.loads),
        Err(crate::instrument_image::Error::Stopped)
    );
    f.assert_retained();
}
#[test]
fn instrument_upload_stuck_drain_and_uncertain_link_block_without_reuse() {
    for mode in [0, 3] {
        let mut f = Fixture::new();
        f.state.mode.set(mode);
        let state = f.state.clone();
        let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
        let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 4).unwrap();
        upload.tick(0);
        if mode == 0 {
            assert_eq!(upload.tick(4), Progress::Pending);
        }
        let failure = if mode == 0 {
            Failure::Timeout
        } else {
            Failure::Link
        };
        assert_eq!(
            upload.tick(if mode == 0 { 8 } else { 1 }),
            Progress::Blocked(failure)
        );
        let polls = state.polls.get();
        upload.tick(9);
        assert_eq!(state.polls.get(), polls);
        drop(upload);
        assert_eq!(state.starts.get(), 1);
        f.assert_retained();
    }
}
#[test]
fn instrument_upload_busy_retries_share_wrapping_deadline() {
    let mut f = Fixture::new();
    f.state.busy.set(true);
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    let start = u32::MAX - 2;
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, start, 4).unwrap();
    for i in 0..4 {
        assert_eq!(upload.tick(start.wrapping_add(i)), Progress::Pending);
    }
    assert_eq!(
        upload.tick(start.wrapping_add(4)),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(f.ticket)
        }
    );
    drop(upload);
    assert_eq!(f.state.starts.get(), 0);
    f.assert_retained();
}
#[test]
fn instrument_upload_drained_failure_requests_detach() {
    let mut f = Fixture::new();
    f.state.mode.set(2);
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 10).unwrap();
    upload.tick(0);
    assert_eq!(
        upload.tick(1),
        Progress::Recover {
            cause: Failure::Link,
            command: Command::Detach(f.ticket)
        }
    );
    drop(upload);
    f.assert_retained();
}
#[test]
fn instrument_upload_boot_loss_permanently_stops_driver_and_retains_memory() {
    let mut f = Fixture::new();
    let state = f.state.clone();
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 10).unwrap();
    upload.tick(0);
    upload.invalidate_boot();
    assert_eq!(upload.tick(1), Progress::Blocked(Failure::Boot));
    drop(upload);
    assert_eq!(f.driver.poll(), Err(LinkError::Uncertain));
    assert_eq!(state.polls.get(), 0);
    assert_eq!(
        f.driver
            .start_image(&transfer.next(&f.loads).unwrap().unwrap()),
        Err(LinkError::Uncertain)
    );
    f.assert_retained();
}

#[test]
fn instrument_upload_deadline_wins_over_final_ack_and_invalid_budgets_do_not_send() {
    let mut f = Fixture::new();
    let mut transfer = Transfer::begin(&mut f.loads, f.ticket, &f.grant, &IMAGE, true).unwrap();
    for budget in [0, 0x80000000, u32::MAX] {
        assert!(Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, budget).is_none());
    }
    assert_eq!(f.state.starts.get(), 0);
    let mut upload = Upload::new(&mut f.driver, &mut transfer, &mut f.loads, 0, 7).unwrap();
    for now in 0..7 {
        assert_eq!(upload.tick(now), Progress::Pending);
    }
    assert_eq!(
        upload.tick(7),
        Progress::Recover {
            cause: Failure::Timeout,
            command: Command::Detach(f.ticket)
        }
    );
    drop(upload);
    assert!(transfer.install_ticket(&f.loads).is_err());
    f.assert_retained();
}
