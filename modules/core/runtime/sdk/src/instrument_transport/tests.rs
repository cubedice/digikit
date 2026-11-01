extern crate std;
use super::*;
use crate::instrument_load::{Command, Loads};
use crate::instrument_registry::Request;
use std::boxed::Box;

struct Mock {
    start_result: i32,
    id: u32,
    poll_result: i32,
    state: u32,
    error: u32,
    source: *const u8,
    len: usize,
    window: u32,
    destination: u32,
    starts: u32,
}
impl Mock {
    fn new() -> Self {
        Self {
            start_result: 0,
            id: 1,
            poll_result: 0,
            state: 1,
            error: 0,
            source: core::ptr::null(),
            len: 0,
            window: 0,
            destination: 0,
            starts: 0,
        }
    }
    fn bytes(&self) -> &[u8] {
        unsafe { core::slice::from_raw_parts(self.source, self.len) }
    }
}
unsafe impl Transport for Mock {
    unsafe fn start(
        &mut self,
        window: u32,
        destination: u32,
        source: *const u8,
        bytes: u32,
    ) -> (i32, u32) {
        self.starts += 1;
        if self.start_result == 0 {
            self.source = source;
            self.len = bytes as usize;
            self.window = window;
            self.destination = destination;
        }
        (self.start_result, self.id)
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        assert_eq!(id, self.id);
        status.state = self.state;
        status.err = self.error;
        self.poll_result
    }
}
fn driver() -> Driver<Mock> {
    Driver::new(Mock::new(), Box::leak(Box::new([0; 256])))
}
fn ticket(loads: &mut Loads) -> crate::instrument_registry::Ticket {
    loads
        .reserve(
            0,
            &[Request {
                type_no: 20,
                tracks: 1,
            }],
        )
        .unwrap()
}
fn write<'a>(t: crate::instrument_registry::Ticket, data: &'a [u8]) -> image::Write<'a> {
    image::Write {
        ack: image::Ack {
            ticket: t,
            address: 0x20010000,
            bytes: data.len() as u32,
        },
        data,
    }
}
fn done(driver: &mut Driver<Mock>) -> Completed {
    driver.transport.poll_result = 1;
    driver.transport.state = 2;
    match driver.poll().unwrap() {
        Progress::Complete(c) => c,
        _ => panic!("no completion"),
    }
}

#[test]
fn instrument_transport_retains_image_bytes_across_moves_and_busy_retries() {
    let mut loads = Loads::new(16);
    let t = ticket(&mut loads);
    let mut driver = driver();
    let mut source = [1, 2, 3, 4];
    driver.transport.start_result = -1;
    assert_eq!(driver.start_image(&write(t, &source)), Ok(Started::Busy));
    assert_eq!(driver.poll(), Ok(Progress::Idle));
    driver.transport.start_result = 0;
    assert_eq!(
        driver.start_image(&write(t, &source)),
        Ok(Started::Accepted)
    );
    source.fill(9);
    let mut moved = driver;
    assert_eq!(moved.transport.bytes(), &[1, 2, 3, 4]);
    assert_eq!(moved.start_image(&write(t, &source)), Err(Error::Busy));
    assert_eq!(moved.transport.bytes(), &[1, 2, 3, 4]);
    moved.transport.state = 2; // DONE, but END frames are still pending
    assert_eq!(moved.poll(), Ok(Progress::Pending));
    assert_eq!(
        done(&mut moved).target,
        Target::Image(write(t, &[0; 4]).ack)
    );
    assert_eq!(moved.poll(), Ok(Progress::Idle));
}

#[test]
fn instrument_transport_mailbox_encoding_and_acks_do_not_publish() {
    let mut loads = Loads::new(16);
    let t = ticket(&mut loads);
    loads.start(t).unwrap();
    loads.image_acked(t).unwrap();
    let mut mailbox = mailbox::Mailbox::new();
    mailbox.begin(Command::Initialize(t), &loads, true).unwrap();
    let mut driver = driver();
    for stage in 1..=3 {
        let write = mailbox.write().unwrap();
        assert_eq!(driver.start_mailbox(&write), Ok(Started::Accepted));
        assert_eq!(driver.transport.window, 3);
        assert_eq!(driver.transport.destination, write.offset);
        for (bytes, word) in driver
            .transport
            .bytes()
            .chunks_exact(4)
            .zip(&write.words[..write.count])
        {
            assert_eq!(bytes, &word.to_le_bytes());
        }
        assert_eq!(mailbox.write().unwrap().stage, stage);
        done(&mut driver).mailbox(&mut mailbox).unwrap();
        driver.transport.id += 1;
    }
    assert!(mailbox.write().is_none()); // still requires a separate DSP registry reply
    assert!(loads.lookup(20).is_none());
}

#[test]
fn instrument_transport_image_completion_advances_the_exact_span() {
    use crate::instrument_load::{Ledger, MemoryRequest};
    let mut loads = Loads::new(16);
    let t = ticket(&mut loads);
    let (mut offsets, mut lengths) = ([0], [0]);
    let grant = loads
        .reserve_memory(
            t,
            &mut Ledger {
                base: 0x20010000,
                bytes: 1024,
                offsets: &mut offsets,
                lengths: &mut lengths,
            },
            &MemoryRequest {
                image_bytes: 8,
                bss_bytes: 4,
                state_bytes: 16,
                align: 8,
            },
        )
        .unwrap();
    let mut image = image::Transfer::begin(&mut loads, t, &grant, &[0x5a; 8], true).unwrap();
    let mut driver = driver();
    for expected in [&[0x5a; 8][..], &[0; 8][..]] {
        let write = image.next(&loads).unwrap().unwrap();
        driver.start_image(&write).unwrap();
        assert_eq!(driver.transport.bytes(), expected);
        assert_eq!(driver.transport.window, 0);
        assert_eq!(driver.transport.destination, write.ack.address);
        done(&mut driver).image(&mut image, &loads).unwrap();
        driver.transport.id += 1;
    }
    assert!(image.next(&loads).unwrap().is_none());
    assert!(loads.lookup(20).is_none());
}

#[test]
fn instrument_transport_failure_and_uncertain_identity_never_ack() {
    let mut loads = Loads::new(16);
    let t = ticket(&mut loads);
    let mut driver = driver();
    driver.start_image(&write(t, &[1; 4])).unwrap();
    driver.transport.state = 3;
    driver.transport.error = 100;
    assert_eq!(driver.poll(), Ok(Progress::Pending));
    driver.transport.poll_result = 1;
    assert_eq!(driver.poll(), Ok(Progress::Failed(100)));
    driver.transport.id = 2;
    driver.start_image(&write(t, &[2; 4])).unwrap();
    driver.transport.poll_result = -1;
    assert_eq!(driver.poll(), Err(Error::Uncertain));
    assert_eq!(
        driver.start_image(&write(t, &[3; 4])),
        Err(Error::Uncertain)
    );
    assert_eq!(driver.transport.bytes(), &[2; 4]);
}

#[test]
fn instrument_transport_rejects_invalid_shapes_and_repeated_accepted_ids() {
    let mut loads = Loads::new(16);
    let t = ticket(&mut loads);
    let mut driver = driver();
    for data in [&[][..], &[1; 3][..], &[1; 260][..]] {
        assert_eq!(
            driver.start_image(&write(t, data)),
            Err(Error::InvalidWrite)
        );
    }
    let invalid = mailbox::Write {
        sequence: 1,
        stage: 2,
        offset: 1,
        count: 17,
        words: [0; 16],
    };
    assert_eq!(driver.start_mailbox(&invalid), Err(Error::InvalidWrite));
    assert_eq!(driver.transport.starts, 0);
    driver.transport.start_result = -2;
    assert_eq!(driver.start_image(&write(t, &[1; 4])), Err(Error::Rejected));
    driver.transport.start_result = 0;
    driver.start_image(&write(t, &[1; 4])).unwrap();
    done(&mut driver);
    assert_eq!(
        driver.start_image(&write(t, &[2; 4])),
        Err(Error::Uncertain)
    );
    assert_eq!(driver.poll(), Err(Error::Uncertain));
}

#[test]
fn instrument_transport_calls_the_tagged_c_abi() {
    unsafe extern "C" fn send(
        win: u32,
        dst: u32,
        source: *const u8,
        bytes: u32,
        id: *mut u32,
    ) -> i32 {
        if win != 3
            || dst != 0
            || bytes != 4
            || core::slice::from_raw_parts(source, 4) != [0x31, 0x47, 0x52, 0x49]
        {
            return -2;
        }
        *id = 99;
        0
    }
    unsafe extern "C" fn poll(id: u32, status: *mut Status) -> i32 {
        if id != 99 {
            return -1;
        }
        (*status).state = 2;
        1
    }
    let api = unsafe { Api::new(send, poll) };
    let mut driver = Driver::new(api, Box::leak(Box::new([0; 256])));
    let mut words = [0; 16];
    words[0] = 0x49524731;
    let write = mailbox::Write {
        sequence: 123,
        stage: 3,
        offset: 0,
        count: 1,
        words,
    };
    assert_eq!(driver.start_mailbox(&write), Ok(Started::Accepted));
    match driver.poll().unwrap() {
        Progress::Complete(c) => assert_eq!(
            c.target,
            Target::Mailbox {
                sequence: 123,
                stage: 3
            }
        ),
        _ => panic!("no completion"),
    }
}
