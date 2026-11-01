extern crate std;
use super::*;
use crate::{
    instrument_admission::{prepare, Limits, Range, Spec},
    instrument_load::MemoryRequest,
    instrument_provider::Providers,
    instrument_registry::Request,
    instrument_transport::Status,
};
use std::{boxed::Box, cell::RefCell, vec::Vec};

#[derive(Default)]
struct Wire {
    require_lease: bool,
    expected: u32,
    arms: u32,
    arm_error: i32,
    owned: bool,
    claims: u32,
    releases: u32,
    uncertain_release: bool,
    claim_busy: bool,
    release_busy: bool,
    mode: u32,
    busy: bool,
    silent: bool,
    boot_lost: bool,
    snapshot_error: i32,
    reject: u32,
    starts: u32,
    polls: u32,
    snapshots: u32,
    source: Option<(u32, u32, *const u8, usize)>,
    words: [u32; 16],
    reply: [u32; 16],
    operations: Vec<(u32, u32, u32)>,
    image: Vec<u8>,
}
std::thread_local! { static WIRE: RefCell<Wire> = RefCell::new(Wire::default()); }
struct Link;
unsafe impl Transport for Link {
    fn transaction_ready(&self) -> bool {
        WIRE.with_borrow(|w| !w.require_lease || w.owned)
    }
    unsafe fn start(
        &mut self,
        window: u32,
        dest: u32,
        source: *const u8,
        bytes: u32,
    ) -> (i32, u32) {
        WIRE.with_borrow_mut(|w| {
            assert!(!w.require_lease || w.owned);
            if w.busy {
                return (-1, 0);
            }
            assert!(w.source.is_none());
            w.starts += 1;
            w.source = Some((window, dest, source, bytes as usize));
            (0, w.starts)
        })
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        WIRE.with_borrow_mut(|w| {
            assert_eq!(id, w.starts);
            w.polls += 1;
            if w.mode == 0 {
                status.state = 2;
                return 0;
            }
            if w.mode == 3 {
                return -1;
            }
            let (window, dest, source, bytes) = w.source.take().unwrap();
            let data = unsafe { core::slice::from_raw_parts(source, bytes) };
            status.state = if w.mode == 2 { 3 } else { 2 };
            if w.mode == 2 {
                return 1;
            }
            if window == 0 {
                assert_eq!(dest, 0x20010000 + w.image.len() as u32);
                w.image.extend_from_slice(data);
            } else {
                assert_eq!(window, 3);
                for (i, bytes) in data.chunks_exact(4).enumerate() {
                    w.words[dest as usize + i] = u32::from_le_bytes(bytes.try_into().unwrap());
                }
                if w.words[0] != 0 {
                    assert_eq!(w.words[0], 0x49524731);
                    assert_eq!(w.words.iter().fold(0, |a, b| a ^ b), 0);
                    w.operations.push((w.words[2], w.words[5], w.words[6]));
                    w.reply = [0; 16];
                    w.reply[0] = 0x494ea101;
                    w.reply[4..12].copy_from_slice(&[
                        0x49525231,
                        w.words[2],
                        w.words[3],
                        w.words[4],
                        w.words[5],
                        w.words[6],
                        if w.words[5] == w.reject { 1 } else { 0 },
                        7,
                    ]);
                    if w.expected != 0 {
                        w.reply[0] = 0x494ea102;
                        w.reply[12] = w.expected;
                    }
                    if w.words[5] == 5 {
                        assert_eq!(w.expected, w.words[4]);
                        assert_eq!(w.arms, 1);
                        w.reply[0] = 0x494ea102;
                        w.reply[11] = 0;
                        w.reply[12] = w.expected;
                    }
                    let (mut a, mut b) = (0u32, 0u32);
                    for word in &w.reply[..15] {
                        a = a.wrapping_add(*word);
                        b = b.wrapping_add(a);
                    }
                    w.reply[15] = a ^ b;
                }
            }
            1
        })
    }
}

#[test]
fn bound_session_drives_install_initialize_and_rejected_init_detach() {
    use crate::instrument_section::{tests as section_test, Section};
    for reject in [false, true] {
        let mut owner = bootstrap_owner();
        unsafe { owner.begin_session(expect_session, 0x10203040, 2, 100) }.unwrap();
        let bytes = section_test::fixture();
        let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
        let output = Box::leak(Box::new([0; 64]));
        let mut offsets = [0];
        let mut lengths = [0];
        let mut ledger = Ledger {
            base: 0x20010000,
            bytes: 512,
            offsets: &mut offsets,
            lengths: &mut lengths,
        };
        assert!(matches!(owner.admit(0, &section, &[20], &admission_limits(),
            &mut ledger, output, |_| Some(0xb80000)),
            Err(AdmissionError::Coordinator(Error::Busy))));
        assert_eq!(ledger.lengths[0], 0);
        for now in 3..9 {
            assert_eq!(owner.tick(now), Progress::Pending);
        }
        assert_eq!(owner.tick(9), Progress::Idle);
        assert!(owner.session_bound());
        // Exercise the actual handoff between bootstrap and a later load.
        owner.request_link_release(10, 100).unwrap();
        assert_eq!(owner.tick_link(11, true), LinkProgress::Released);
        let plan = owner.admit(0, &section, &[20], &admission_limits(),
            &mut ledger, output, |_| Some(0xb80000)).unwrap();
        let ticket = plan.ticket();
        owner.request_link(12, 100).unwrap();
        assert_eq!(owner.tick_link(13, true), LinkProgress::Owned);
        WIRE.with_borrow_mut(|w| w.reject = if reject { 2 } else { 0 });
        owner.begin(plan, output, 14, 1000).unwrap();
        let mut completed = false;
        for now in 15..1000 {
            match owner.tick(now) {
                Progress::Pending => assert_eq!(owner.loads().lookup(20), None),
                Progress::Complete { command, recovered } => {
                    if reject {
                        assert_eq!(command, Command::Release(ticket));
                        assert_eq!(recovered, Some(Failure::Reply));
                        owner.release_memory(ticket, &mut ledger).unwrap();
                        assert_eq!(ledger.lengths[0], 0);
                    } else {
                        assert_eq!(command, Command::Publish(ticket));
                        assert_eq!(recovered, None);
                        assert!(owner.loads().lookup(20).is_some());
                        assert_eq!(ledger.lengths[0], 128);
                    }
                    completed = true;
                    break;
                }
                other => panic!("session load failed: {other:?}"),
            }
        }
        assert!(completed);
        assert!(owner.session_bound());
        WIRE.with_borrow(|w| {
            let operations: Vec<_> = w.operations.iter().map(|x| (x.0, x.1)).collect();
            let expected = if reject {
                std::vec![(1, 5), (2, 4), (3, 2), (4, 3)]
            } else {
                std::vec![(1, 5), (2, 4), (3, 2)]
            };
            assert_eq!(operations, expected);
            assert_eq!(w.arms, 1);
            assert!(w.source.is_none());
        });
    }
}

impl crate::instrument_lease::LeaseTransport for Link {
    fn claim(&mut self) -> Result<bool, crate::instrument_lease::Error> {
        WIRE.with_borrow_mut(|w| {
            w.claims += 1;
            if w.claim_busy {
                return Ok(false);
            }
            w.owned = true;
            Ok(true)
        })
    }
    fn release(&mut self) -> Result<(), crate::instrument_lease::Error> {
        WIRE.with_borrow_mut(|w| {
            w.releases += 1;
            if w.release_busy {
                return Err(crate::instrument_lease::Error::Busy);
            }
            if w.uncertain_release {
                return Err(crate::instrument_lease::Error::Uncertain);
            }
            assert!(w.source.is_none());
            w.owned = false;
            Ok(())
        })
    }
}

#[test]
fn coordinator_keeps_lease_across_all_phases_until_explicit_release() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.require_lease = true);
    assert_eq!(f.owner.claim_link(), Ok(true));
    f.begin(0, 60);
    for now in 0..60 {
        assert_eq!(
            f.owner.release_link(),
            Err(crate::instrument_lease::Error::Busy)
        );
        assert_eq!(
            f.owner.claim_link(),
            Err(crate::instrument_lease::Error::Busy)
        );
        match f.owner.tick(now) {
            Progress::Pending => continue,
            Progress::Complete {
                command: Command::Publish(ticket),
                recovered: None,
            } => {
                assert_eq!(ticket, f.ticket);
                assert_eq!(f.owner.release_link(), Ok(()));
                WIRE.with_borrow(|w| {
                    assert_eq!((w.claims, w.releases), (1, 1));
                    assert!(!w.owned);
                });
                return;
            }
            other => panic!("unexpected {other:?}"),
        }
    }
    panic!("did not complete");
}

#[test]
fn coordinator_refuses_unowned_start_and_keeps_boot_lost_lease() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.require_lease = true);
    assert_eq!(
        f.owner.begin(f.plan.take().unwrap(), &IMAGE, 0, 60),
        Err(Error::NotOwned)
    );
    assert_eq!(f.owner.tick(0), Progress::Idle);
    assert_eq!(f.owner.claim_link(), Ok(true));
    f.owner.invalidate_boot();
    assert_eq!(
        f.owner.release_link(),
        Err(crate::instrument_lease::Error::Uncertain)
    );
    WIRE.with_borrow(|w| {
        assert_eq!((w.starts, w.releases), (0, 0));
        assert!(w.owned);
    });
}

#[test]
fn uncertain_release_after_recovery_blocks_allocator_and_future_calls() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| {
        w.require_lease = true;
        w.busy = true;
    });
    assert_eq!(f.owner.claim_link(), Ok(true));
    f.begin(0, 20);
    assert_eq!(f.owner.tick(20), Progress::Pending);
    assert_eq!(
        f.owner.release_link(),
        Err(crate::instrument_lease::Error::Busy)
    );
    WIRE.with_borrow_mut(|w| w.busy = false);
    assert_eq!(
        f.run(21, 20),
        Progress::Complete {
            command: Command::Release(f.ticket),
            recovered: Some(Failure::Timeout)
        }
    );
    WIRE.with_borrow_mut(|w| w.uncertain_release = true);
    assert_eq!(
        f.owner.release_link(),
        Err(crate::instrument_lease::Error::Uncertain)
    );
    assert_eq!(
        f.owner.release_link(),
        Err(crate::instrument_lease::Error::Uncertain)
    );
    assert!(f.release().is_err());
    assert_eq!(f.owner.tick(100), Progress::Blocked(Failure::Link));
    WIRE.with_borrow(|w| {
        assert_eq!(w.releases, 1);
        assert!(w.owned);
    });
}
unsafe extern "C" fn unused_send(_: u32, _: u32, _: *const u8, _: u32, _: *mut u32) -> i32 {
    panic!("wrong driver");
}
unsafe extern "C" fn unused_poll(_: u32, _: *mut Status) -> i32 {
    panic!("wrong driver");
}
unsafe extern "C" fn snapshot(out: *mut u32, n: u32) -> i32 {
    assert_eq!(n, 16);
    WIRE.with_borrow_mut(|w| {
        w.snapshots += 1;
        if w.boot_lost {
            return -2;
        }
        if w.snapshot_error != 0 {
            return w.snapshot_error;
        }
        if w.silent {
            return 0;
        }
        core::ptr::copy_nonoverlapping(w.reply.as_ptr(), out, 16);
        16
    })
}
const IMAGE: [u8; 300] = [0xa5; 300];
struct Fixture {
    owner: Coordinator<'static, Link>,
    plan: Option<Plan>,
    ticket: Ticket,
    offsets: [u32; 1],
    lengths: [u32; 1],
}
impl Fixture {
    fn new() -> Self {
        WIRE.with_borrow_mut(|w| {
            *w = Wire {
                mode: 1,
                ..Wire::default()
            }
        });
        let mut loads = Loads::new(16);
        let ticket = loads
            .reserve(
                0,
                &[
                    Request {
                        type_no: 17,
                        tracks: 2,
                    },
                    Request {
                        type_no: 18,
                        tracks: 2,
                    },
                ],
            )
            .unwrap();
        let (mut offsets, mut lengths) = ([0], [0]);
        let grant = loads
            .reserve_memory(
                ticket,
                &mut Ledger {
                    base: 0x20010000,
                    bytes: 2048,
                    offsets: &mut offsets,
                    lengths: &mut lengths,
                },
                &MemoryRequest {
                    image_bytes: 300,
                    bss_bytes: 300,
                    state_bytes: 64,
                    align: 16,
                },
            )
            .unwrap();
        let text = Range {
            start: 0x20010000,
            end: 0x20010100,
        };
        let spec = Spec {
            type_no: 17,
            state_base: grant.state.start,
            state_bytes: 12,
            text,
            process_text: text,
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
            ticket,
            &grant,
            &[
                spec,
                Spec {
                    type_no: 18,
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
        let replies =
            unsafe { Providers::bind(Some(unused_send), Some(unused_poll), Some(snapshot)) }
                .unwrap()
                .into_parts()
                .1;
        let owner = Coordinator::new(
            Driver::new(Link, Box::leak(Box::new([0; 256]))),
            replies,
            loads,
        );
        Self {
            owner,
            plan: Some(plan),
            ticket,
            offsets,
            lengths,
        }
    }
    fn begin(&mut self, now: u32, budget: u32) {
        self.owner
            .begin(self.plan.take().unwrap(), &IMAGE, now, budget)
            .unwrap();
    }
    fn release(&mut self) -> Result<(), MemoryError> {
        self.owner.release_memory(
            self.ticket,
            &mut Ledger {
                base: 0x20010000,
                bytes: 2048,
                offsets: &mut self.offsets,
                lengths: &mut self.lengths,
            },
        )
    }
    fn run(&mut self, start: u32, count: u32) -> Progress {
        for elapsed in 0..count {
            let result = self.owner.tick(start.wrapping_add(elapsed));
            if result != Progress::Pending {
                return result;
            }
            assert!(self.owner.loads().lookup(17).is_none());
        }
        panic!("coordinator did not terminate");
    }
}

#[test]
fn moved_owner_keeps_buffer_and_drives_all_rows_before_publishing() {
    let mut f = Fixture::new();
    f.begin(0, 60);
    assert_eq!(f.owner.tick(0), Progress::Pending); // provider retains buffer
    let mut f = *Box::new(f); // move owner while transfer is in flight
    assert_eq!(
        f.run(1, 60),
        Progress::Complete {
            command: Command::Publish(f.ticket),
            recovered: None
        }
    );
    assert!(f.owner.loads().lookup(17).is_some());
    assert!(f.owner.loads().lookup(18).is_some());
    assert!(f.release().is_err());
    assert_eq!(f.owner.tick(100), Progress::Idle); // completion is emitted once
    WIRE.with_borrow(|w| {
        assert_eq!(&w.image[..300], &IMAGE);
        assert!(w.image[300..].iter().all(|&b| b == 0));
        assert_eq!(w.image.len(), 600);
        assert_eq!(w.operations, [(1, 4, 0), (2, 4, 1), (3, 2, 0), (4, 2, 1)]);
    });
}

#[test]
fn upload_deadline_survives_ticks_and_wrap_then_detaches_before_release() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.busy = true);
    let start = u32::MAX - 3;
    f.begin(start, 20);
    for elapsed in 0..20 {
        assert_eq!(f.owner.tick(start.wrapping_add(elapsed)), Progress::Pending);
    }
    assert_eq!(f.owner.tick(start.wrapping_add(20)), Progress::Pending);
    assert!(f.release().is_err());
    assert_ne!(f.lengths[0], 0);
    WIRE.with_borrow_mut(|w| {
        assert_eq!(w.starts, 0);
        w.busy = false;
    });
    assert_eq!(
        f.run(start.wrapping_add(21), 20),
        Progress::Complete {
            command: Command::Release(f.ticket),
            recovered: Some(Failure::Timeout),
        }
    );
    assert_ne!(f.lengths[0], 0); // caller must explicitly release original ledger
    f.release().unwrap();
    assert_eq!(f.lengths[0], 0);
    WIRE.with_borrow(|w| assert_eq!(w.operations, [(1, 3, 0), (2, 3, 1)]));
}

#[test]
fn rejected_install_or_initialize_uses_fresh_detach_sequences() {
    for operation in [4, 2] {
        let mut f = Fixture::new();
        WIRE.with_borrow_mut(|w| w.reject = operation);
        f.begin(0, 60);
        assert_eq!(
            f.run(0, 80),
            Progress::Complete {
                command: Command::Release(f.ticket),
                recovered: Some(Failure::Reply),
            }
        );
        assert!(f.owner.loads().lookup(17).is_none());
        f.release().unwrap();
        WIRE.with_borrow(|w| {
            assert!(w.operations.windows(2).all(|pair| pair[0].0 < pair[1].0));
            assert!(w.operations.ends_with(&[
                (w.operations.len() as u32 - 1, 3, 0),
                (w.operations.len() as u32, 3, 1)
            ]));
        });
    }
}

#[test]
fn failed_detach_blocks_without_infinite_retries_or_storage_reuse() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| {
        w.busy = true;
        w.reject = 3;
    });
    f.begin(0, 20);
    assert_eq!(f.owner.tick(20), Progress::Pending);
    WIRE.with_borrow_mut(|w| w.busy = false);
    assert_eq!(f.run(21, 30), Progress::Blocked(Failure::Reply));
    let counts = WIRE.with_borrow(|w| (w.starts, w.polls, w.snapshots));
    assert_eq!(f.owner.tick(100), Progress::Blocked(Failure::Reply));
    WIRE.with_borrow(|w| assert_eq!((w.starts, w.polls, w.snapshots), counts));
    assert!(f.release().is_err());
    assert_ne!(f.lengths[0], 0);
}

#[test]
fn undrained_timeout_retains_source_and_blocks_all_further_work() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.mode = 0);
    f.begin(0, 20);
    assert_eq!(f.owner.tick(0), Progress::Pending);
    assert_eq!(f.owner.tick(20), Progress::Pending);
    assert_eq!(f.owner.tick(40), Progress::Blocked(Failure::Timeout));
    assert_eq!(f.owner.tick(41), Progress::Blocked(Failure::Timeout));
    assert!(f.release().is_err());
    WIRE.with_borrow(|w| {
        assert_eq!(w.starts, 1);
        assert_eq!(w.polls, 2);
        let (_, _, source, bytes) = w.source.unwrap();
        assert_eq!(
            unsafe { core::slice::from_raw_parts(source, bytes) },
            &IMAGE[..256]
        );
    });
}

#[test]
fn boot_loss_during_upload_or_reply_wait_never_sends_detach() {
    for explicit in [true, false] {
        let mut f = Fixture::new();
        f.begin(0, 60);
        assert_eq!(f.owner.tick(0), Progress::Pending);
        if explicit {
            f.owner.invalidate_boot();
        } else {
            WIRE.with_borrow_mut(|w| w.boot_lost = true);
        }
        assert_eq!(f.run(1, 60), Progress::Blocked(Failure::Boot));
        let counts = WIRE.with_borrow(|w| (w.starts, w.polls, w.snapshots));
        assert_eq!(f.owner.tick(100), Progress::Blocked(Failure::Boot));
        assert!(f.release().is_err());
        WIRE.with_borrow(|w| {
            assert_eq!((w.starts, w.polls, w.snapshots), counts);
            assert!(w.operations.iter().all(|(_, op, _)| *op != 3));
        });
    }
}

#[test]
fn invalid_budget_changes_neither_admission_nor_wire() {
    for budget in [0, 0x80000000, u32::MAX] {
        let mut f = Fixture::new();
        assert_eq!(
            f.owner.begin(f.plan.take().unwrap(), &IMAGE, 0, budget),
            Err(Error::Budget)
        );
        WIRE.with_borrow(|w| assert_eq!((w.starts, w.polls, w.snapshots), (0, 0, 0)));
        assert_eq!(f.owner.tick(0), Progress::Idle);
        assert_eq!(
            f.owner.loads.start(f.ticket),
            Ok(Command::SendImage(f.ticket))
        );
        WIRE.with_borrow(|w| assert_eq!((w.starts, w.polls, w.snapshots), (0, 0, 1)));
    }
}

#[test]
fn missing_replies_exhaust_install_and_one_detach_deadline() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.silent = true);
    f.begin(0, 20);
    assert_eq!(f.run(0, 80), Progress::Blocked(Failure::Timeout));
    assert!(f.release().is_err());
    assert_ne!(f.lengths[0], 0);
    WIRE.with_borrow(|w| assert_eq!(w.operations, [(1, 4, 0), (2, 3, 0)]));
}

#[test]
fn unknown_transfer_identity_blocks_without_attempting_detach() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.mode = 3);
    f.begin(0, 20);
    assert_eq!(f.owner.tick(0), Progress::Pending);
    assert_eq!(f.owner.tick(1), Progress::Blocked(Failure::Link));
    assert_eq!(f.owner.tick(100), Progress::Blocked(Failure::Link));
    assert!(f.release().is_err());
    WIRE.with_borrow(|w| {
        assert_eq!((w.starts, w.polls), (1, 1));
        assert!(w.operations.is_empty());
    });
}

#[test]
fn boot_loss_after_detach_completion_prevents_allocator_reuse() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.busy = true);
    f.begin(0, 20);
    assert_eq!(f.owner.tick(20), Progress::Pending);
    WIRE.with_borrow_mut(|w| w.busy = false);
    assert_eq!(
        f.run(21, 20),
        Progress::Complete {
            command: Command::Release(f.ticket),
            recovered: Some(Failure::Timeout),
        }
    );
    f.owner.invalidate_boot();
    assert!(f.release().is_err());
    assert_ne!(f.lengths[0], 0);
}

#[test]
fn handoff_waits_for_legacy_then_keeps_release_exclusive() {
    let mut f = Fixture::new();
    WIRE.with_borrow_mut(|w| w.require_lease = true);
    assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Run);
    assert_eq!(f.owner.tick_link(0, false), LinkProgress::Released);
    assert_eq!(f.owner.request_link(10, 20), Ok(()));
    assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Drain);
    assert_eq!(f.owner.tick_link(11, false), LinkProgress::Pending);
    WIRE.with_borrow(|w| assert_eq!(w.claims, 0));
    WIRE.with_borrow_mut(|w| w.claim_busy = true);
    assert_eq!(f.owner.tick_link(12, true), LinkProgress::Pending);
    WIRE.with_borrow_mut(|w| w.claim_busy = false);
    assert_eq!(f.owner.tick_link(13, true), LinkProgress::Owned);
    assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Pause);
    f.begin(14, 1000);
    assert_eq!(f.owner.request_link_release(15, 10), Err(Error::Busy));
    assert_eq!(f.owner.tick_link(15, false), LinkProgress::Owned);
    for now in 16..1000 {
        if matches!(f.owner.tick(now), Progress::Complete { .. }) {
            assert_eq!(f.owner.request_link_release(now, 10), Ok(()));
            assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Pause);
            WIRE.with_borrow_mut(|w| w.release_busy = true);
            assert_eq!(f.owner.tick_link(now, true), LinkProgress::Pending);
            WIRE.with_borrow(|w| assert!(w.owned));
            assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Pause);
            WIRE.with_borrow_mut(|w| w.release_busy = false);
            assert_eq!(f.owner.tick_link(now + 1, true), LinkProgress::Released);
            assert_eq!(f.owner.tick_link(now + 2, true), LinkProgress::Released);
            assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Run);
            WIRE.with_borrow(|w| {
                assert!(!w.owned);
                assert_eq!((w.claims, w.releases), (2, 2));
            });
            return;
        }
    }
    panic!("load did not finish");
}

#[test]
fn acquisition_deadline_includes_legacy_drain_and_wraps_without_renewal() {
    let mut f = Fixture::new();
    assert_eq!(f.owner.request_link(u32::MAX - 2, 5), Ok(()));
    assert_eq!(f.owner.tick_link(0, false), LinkProgress::Pending);
    assert_eq!(f.owner.request_link(0, 100), Err(Error::Busy));
    WIRE.with_borrow_mut(|w| w.claim_busy = true);
    assert_eq!(f.owner.tick_link(1, true), LinkProgress::Pending);
    assert_eq!(
        f.owner.tick_link(2, true),
        LinkProgress::Blocked(Failure::Timeout)
    );
    assert_eq!(
        f.owner.tick_link(3, true),
        LinkProgress::Blocked(Failure::Timeout)
    );
    assert_eq!(f.owner.request_link(3, 100), Err(Error::Blocked));
    WIRE.with_borrow(|w| assert_eq!((w.claims, w.releases), (1, 0)));
}

#[test]
fn release_timeout_retains_lease_and_blocks_memory_reuse() {
    let mut f = Fixture::new();
    assert_eq!(f.owner.request_link(0, 10), Ok(()));
    assert_eq!(f.owner.tick_link(0, true), LinkProgress::Owned);
    assert_eq!(f.owner.request_link_release(1, 2), Ok(()));
    WIRE.with_borrow_mut(|w| w.release_busy = true);
    assert_eq!(f.owner.tick_link(2, true), LinkProgress::Pending);
    assert_eq!(f.owner.request_link_release(2, 20), Err(Error::Busy));
    assert_eq!(
        f.owner.tick_link(3, true),
        LinkProgress::Blocked(Failure::Timeout)
    );
    assert!(f.release().is_err());
    assert_eq!(f.owner.tick(4), Progress::Blocked(Failure::Timeout));
    assert_eq!(
        f.owner.tick_link(4, true),
        LinkProgress::Blocked(Failure::Timeout)
    );
    WIRE.with_borrow(|w| {
        assert!(w.owned);
        assert_eq!(w.releases, 1);
    });
}

#[test]
fn boot_loss_stops_handoff_callbacks_in_all_wait_states() {
    for acquired in [false, true] {
        let mut f = Fixture::new();
        assert_eq!(f.owner.request_link(0, 10), Ok(()));
        if acquired {
            assert_eq!(f.owner.tick_link(0, true), LinkProgress::Owned);
            assert_eq!(f.owner.request_link_release(1, 10), Ok(()));
        }
        f.owner.invalidate_boot();
        assert_eq!(
            f.owner.tick_link(2, true),
            LinkProgress::Blocked(Failure::Boot)
        );
        WIRE.with_borrow(|w| {
            assert_eq!(w.claims, u32::from(acquired));
            assert_eq!(w.releases, 0);
            assert_eq!(w.owned, acquired);
        });
    }
}

#[test]
fn handoff_rejects_invalid_budgets_and_loads_during_waits() {
    let mut f = Fixture::new();
    for budget in [0, 0x80000000, u32::MAX] {
        assert_eq!(f.owner.request_link(0, budget), Err(Error::Budget));
    }
    assert_eq!(f.owner.request_link(0, 10), Ok(()));
    assert_eq!(
        f.owner.begin(f.plan.take().unwrap(), &IMAGE, 0, 10),
        Err(Error::Busy)
    );
    assert_eq!(f.owner.tick_link(0, true), LinkProgress::Owned);
    assert_eq!(f.owner.request_link_release(1, 0), Err(Error::Budget));
    assert_eq!(f.owner.tick_link(2, false), LinkProgress::Owned);
}

#[test]
fn uncertain_handoff_never_restores_legacy_pollers() {
    let mut f = Fixture::new();
    f.owner.request_link(0, 10).unwrap();
    assert_eq!(f.owner.tick_link(0, true), LinkProgress::Owned);
    f.owner.request_link_release(1, 10).unwrap();
    WIRE.with_borrow_mut(|w| w.uncertain_release = true);
    assert_eq!(
        f.owner.tick_link(2, true),
        LinkProgress::Blocked(Failure::Link)
    );
    assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Pause);
    assert_eq!(
        f.owner.tick_link(3, true),
        LinkProgress::Blocked(Failure::Link)
    );
    WIRE.with_borrow(|w| assert_eq!(w.releases, 1));
}

#[test]
fn boot_loss_pauses_legacy_even_before_claim_and_after_release() {
    for acquired in [false, true] {
        let mut f = Fixture::new();
        if acquired {
            f.owner.request_link(0, 10).unwrap();
            assert_eq!(f.owner.tick_link(0, true), LinkProgress::Owned);
            f.owner.request_link_release(1, 10).unwrap();
            assert_eq!(f.owner.tick_link(1, true), LinkProgress::Released);
        }
        assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Run);
        f.owner.invalidate_boot();
        assert_eq!(f.owner.legacy_policy(), LegacyPolicy::Pause);
        assert_eq!(
            f.owner.tick_link(2, true),
            LinkProgress::Blocked(Failure::Boot)
        );
    }
}

fn admission_owner() -> Coordinator<'static, Link> {
    WIRE.with_borrow_mut(|w| {
        *w = Wire {
            mode: 1,
            require_lease: true,
            ..Wire::default()
        }
    });
    let replies = unsafe { Providers::bind(Some(unused_send), Some(unused_poll), Some(snapshot)) }
        .unwrap()
        .into_parts()
        .1;
    Coordinator::new(
        Driver::new(Link, Box::leak(Box::new([0; 256]))),
        replies,
        Loads::new(16),
    )
}
fn admission_limits() -> Limits {
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

#[test]
fn admitted_section_uses_shared_ledger_and_publishes_only_after_initialization() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = admission_owner();
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let context = crate::instrument_types::Context {
        selection: 3,
        indices: &[3],
        keys: &[20],
        types: &[0],
        first: 16,
        last: 23,
    };
    let resolved = section.resolve_types(&context, |_| false).unwrap();
    let output = Box::leak(Box::new([0u8; 64]));
    let mut offsets = [0, 0, 128];
    let mut lengths = [0, 0, 64]; // an unrelated legacy module occupies the next range
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let plan = owner
        .admit(
            1,
            &section,
            resolved.as_slice(),
            &admission_limits(),
            &mut ledger,
            output,
            |_| Some(0xb80000),
        )
        .unwrap();
    let ticket = plan.ticket();
    assert_eq!(ledger.lengths, &[0, 128, 64]);
    assert_eq!(owner.loads().capacity().tracks_used, 2);
    assert_eq!(owner.loads().lookup(20), None);
    WIRE.with_borrow(|w| assert_eq!((w.starts, w.claims, w.snapshots), (0, 0, 0)));
    owner.request_link(0, 100).unwrap();
    assert_eq!(owner.tick_link(1, true), LinkProgress::Owned);
    owner.begin(plan, output, 2, 1000).unwrap();
    assert!(matches!(
        owner.cancel_admission(ticket, &mut ledger),
        Err(AdmissionError::Coordinator(Error::Busy))
    ));
    for now in 3..1000 {
        match owner.tick(now) {
            Progress::Pending => assert_eq!(owner.loads().lookup(20), None),
            Progress::Complete {
                command: Command::Publish(done),
                recovered: None,
            } => {
                assert_eq!(done, ticket);
                assert!(owner.loads().lookup(20).is_some());
                assert_eq!(owner.process_reserved(), 40);
                assert!(matches!(
                    owner.cancel_admission(ticket, &mut ledger),
                    Err(AdmissionError::Registry(_))
                ));
                assert_eq!(ledger.lengths, &[0, 128, 64]);
                WIRE.with_borrow(|w| {
                    assert_eq!(&w.image[..64], &output[..]);
                    assert_eq!(&w.image[64..], &[0; 8]);
                    assert_eq!(w.operations.iter().map(|x| x.1).collect::<Vec<_>>(), [4, 2]);
                });
                return;
            }
            other => panic!("unexpected admission progress: {other:?}"),
        }
    }
    panic!("admitted image never initialized");
}

#[test]
fn admission_failures_restore_rows_memory_and_output_without_traffic() {
    use crate::instrument_section::{tests as section_test, Section};
    for failure in 0..4 {
        let mut owner = admission_owner();
        let bytes = section_test::fixture();
        let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
        let mut output = [0xa5; 64];
        let mut offsets = [0, 0, 128];
        let mut lengths = [0, 0, 64];
        let mut ledger = Ledger {
            base: 0x20010000,
            bytes: if failure == 0 { 64 } else { 512 },
            offsets: &mut offsets,
            lengths: &mut lengths,
        };
        let mut limits = admission_limits();
        if failure == 1 {
            limits.process_total = 1;
        }
        let types: &[u32] = if failure == 3 { &[] } else { &[20] };
        assert!(owner
            .admit(
                1,
                &section,
                types,
                &limits,
                &mut ledger,
                &mut output,
                |_| if failure == 2 { None } else { Some(0xb80000) }
            )
            .is_err());
        assert_eq!(ledger.lengths, &[0, 0, 64]);
        assert_eq!(ledger.offsets, &[0, 0, 128]);
        assert_eq!(owner.loads().capacity().rows_used, 0);
        assert_eq!(owner.loads().capacity().tracks_used, 0);
        assert_eq!(output, [0xa5; 64]);
        assert_eq!(owner.legacy_policy(), LegacyPolicy::Run);
        WIRE.with_borrow(|w| assert_eq!((w.starts, w.claims, w.snapshots), (0, 0, 0)));
        // A failed preparation must not strand the same owner or type number.
        ledger.bytes = 512;
        let plan = owner
            .admit(
                1,
                &section,
                &[20],
                &admission_limits(),
                &mut ledger,
                &mut output,
                |_| Some(0xb80000),
            )
            .unwrap();
        owner.cancel_admission(plan.ticket(), &mut ledger).unwrap();
        assert_eq!(ledger.lengths, &[0, 0, 64]);
    }
}

#[test]
fn cancellation_refuses_changed_ledger_and_blocks_further_admission() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = admission_owner();
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let mut output = [0; 64];
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let plan = owner
        .admit(
            0,
            &section,
            &[20],
            &admission_limits(),
            &mut ledger,
            &mut output,
            |_| Some(0xb80000),
        )
        .unwrap();
    ledger.lengths[0] = 256;
    assert_eq!(
        owner.cancel_admission(plan.ticket(), &mut ledger),
        Err(AdmissionError::Cleanup(MemoryError::LedgerChanged))
    );
    assert_eq!(ledger.lengths[0], 256);
    assert_eq!(owner.legacy_policy(), LegacyPolicy::Pause);
    assert!(matches!(
        owner.admit(
            0,
            &section,
            &[20],
            &admission_limits(),
            &mut ledger,
            &mut output,
            |_| Some(0xb80000)
        ),
        Err(AdmissionError::Coordinator(Error::Blocked))
    ));
    WIRE.with_borrow(|w| assert_eq!(w.starts, 0));
}

#[test]
fn waiting_or_boot_invalidated_coordinator_cannot_prepare_images() {
    use crate::instrument_section::{tests as section_test, Section};
    for boot_lost in [false, true] {
        let mut owner = admission_owner();
        let bytes = section_test::fixture();
        let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
        let mut output = [0xa5; 64];
        let mut offsets = [0];
        let mut lengths = [0];
        let mut ledger = Ledger {
            base: 0x20010000,
            bytes: 512,
            offsets: &mut offsets,
            lengths: &mut lengths,
        };
        if boot_lost {
            owner.invalidate_boot();
        } else {
            owner.request_link(0, 10).unwrap();
        }
        assert!(matches!(
            owner.admit(
                0,
                &section,
                &[20],
                &admission_limits(),
                &mut ledger,
                &mut output,
                |_| panic!("must not resolve imports")
            ),
            Err(AdmissionError::Coordinator(_))
        ));
        assert_eq!(ledger.lengths, &[0]);
        assert_eq!(output, [0xa5; 64]);
    }
}

#[test]
fn admission_budgets_include_other_prepared_modules_and_return_on_cancel() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = admission_owner();
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let mut a = [0; 64];
    let mut b = [0xa5; 64];
    let mut offsets = [0; 2];
    let mut lengths = [0; 2];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let mut limits = admission_limits();
    limits.process_total = 60; // each module needs 2 tracks * 20 instructions
    let first = owner
        .admit(0, &section, &[20], &limits, &mut ledger, &mut a, |_| {
            Some(0xb80000)
        })
        .unwrap();
    assert_eq!(owner.process_reserved(), 40);
    assert!(matches!(
        owner.admit(1, &section, &[21], &limits, &mut ledger, &mut b, |_| Some(
            0xb80000
        )),
        Err(AdmissionError::Plan(_))
    ));
    assert_eq!(owner.process_reserved(), 40);
    assert_eq!(ledger.lengths, &[128, 0]);
    assert_eq!(b, [0xa5; 64]);
    limits.process_total = 39;
    assert!(matches!(
        owner.admit(
            1,
            &section,
            &[21],
            &limits,
            &mut ledger,
            &mut b,
            |_| panic!("over budget")
        ),
        Err(AdmissionError::Budget)
    ));
    owner.cancel_admission(first.ticket(), &mut ledger).unwrap();
    assert_eq!(owner.process_reserved(), 0);
    limits.process_total = 80;
    let first = owner
        .admit(0, &section, &[20], &limits, &mut ledger, &mut a, |_| {
            Some(0xb80000)
        })
        .unwrap();
    let second = owner
        .admit(1, &section, &[21], &limits, &mut ledger, &mut b, |_| {
            Some(0xb80000)
        })
        .unwrap();
    assert_eq!(owner.process_reserved(), 80); // exact boundary admits both
    owner
        .cancel_admission(second.ticket(), &mut ledger)
        .unwrap();
    assert_eq!(owner.process_reserved(), 40);
    owner.cancel_admission(first.ticket(), &mut ledger).unwrap();
    assert_eq!(owner.process_reserved(), 0);
}

#[test]
fn processing_cost_survives_failed_init_until_confirmed_detach_and_memory_release() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = admission_owner();
    WIRE.with_borrow_mut(|w| w.reject = 2);
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let output = Box::leak(Box::new([0; 64]));
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let plan = owner
        .admit(
            0,
            &section,
            &[20],
            &admission_limits(),
            &mut ledger,
            output,
            |_| Some(0xb80000),
        )
        .unwrap();
    let ticket = plan.ticket();
    owner.request_link(0, 100).unwrap();
    assert_eq!(owner.tick_link(0, true), LinkProgress::Owned);
    owner.begin(plan, output, 1, 1000).unwrap();
    assert!(owner.release_memory(ticket, &mut ledger).is_err());
    for now in 2..1000 {
        assert_eq!(owner.process_reserved(), 40);
        match owner.tick(now) {
            Progress::Pending => (),
            Progress::Complete {
                command: Command::Release(done),
                recovered: Some(_),
            } => {
                assert_eq!(done, ticket);
                assert_eq!(owner.process_reserved(), 40);
                assert_eq!(ledger.lengths[0], 128);
                owner.release_memory(ticket, &mut ledger).unwrap();
                assert_eq!(owner.process_reserved(), 0);
                assert_eq!(ledger.lengths[0], 0);
                return;
            }
            other => panic!("unexpected recovery: {other:?}"),
        }
    }
    panic!("detach never completed");
}

#[test]
fn externally_adopted_reservations_cannot_bypass_aggregate_budget_accounting() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut f = Fixture::new(); // pre-admitted outside the coordinator
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let mut output = [0xa5; 64];
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    assert!(matches!(
        f.owner.admit(
            0,
            &section,
            &[20],
            &admission_limits(),
            &mut ledger,
            &mut output,
            |_| panic!("unknown budgets")
        ),
        Err(AdmissionError::Unaccounted)
    ));
    assert_eq!(output, [0xa5; 64]);
    assert_eq!(ledger.lengths[0], 0);
}

#[test]
fn detected_boot_loss_preempts_claim_owned_idle_and_release() {
    for stage in 0..3 {
        let mut owner = admission_owner();
        owner.request_link(0, 20).unwrap();
        if stage != 0 {
            assert_eq!(owner.tick_link(0, true), LinkProgress::Owned);
        }
        if stage == 2 {
            owner.request_link_release(1, 20).unwrap();
        }
        WIRE.with_borrow_mut(|w| w.boot_lost = true);
        let before = WIRE.with_borrow(|w| (w.claims, w.releases, w.starts, w.polls));
        assert_eq!(
            owner.tick_link(2, true),
            LinkProgress::Blocked(Failure::Boot)
        );
        assert_eq!(owner.legacy_policy(), LegacyPolicy::Pause);
        WIRE.with_borrow(|w| assert_eq!((w.claims, w.releases, w.starts, w.polls), before));
        let snapshots = WIRE.with_borrow(|w| w.snapshots);
        assert_eq!(
            owner.tick_link(3, true),
            LinkProgress::Blocked(Failure::Boot)
        );
        assert_eq!(owner.tick(3), Progress::Blocked(Failure::Boot));
        WIRE.with_borrow(|w| assert_eq!(w.snapshots, snapshots));
    }
}

#[test]
fn detected_boot_loss_stops_upload_before_polling_or_reusing_its_buffer() {
    let mut f = Fixture::new();
    f.begin(0, 100);
    assert_eq!(f.owner.tick(0), Progress::Pending);
    WIRE.with_borrow_mut(|w| w.boot_lost = true);
    assert_eq!(f.owner.tick(1), Progress::Blocked(Failure::Boot));
    WIRE.with_borrow(|w| {
        assert_eq!((w.starts, w.polls), (1, 0));
        let (_, _, ptr, bytes) = w.source.unwrap();
        assert_eq!(
            unsafe { core::slice::from_raw_parts(ptr, bytes) },
            &IMAGE[..256]
        );
        assert!(w.operations.is_empty());
    });
    assert!(f.release().is_err());
}

#[test]
fn malformed_guard_provider_results_stop_handoff_without_link_callbacks() {
    for result in [-1, 15, 17] {
        let mut owner = admission_owner();
        owner.request_link(0, 20).unwrap();
        WIRE.with_borrow_mut(|w| w.snapshot_error = result);
        assert_eq!(
            owner.tick_link(1, true),
            LinkProgress::Blocked(Failure::Reply)
        );
        assert_eq!(
            owner.tick_link(2, true),
            LinkProgress::Blocked(Failure::Reply)
        );
        WIRE.with_borrow(|w| {
            assert_eq!(
                (w.claims, w.releases, w.starts, w.polls, w.snapshots),
                (0, 0, 0, 0, 1)
            )
        });
    }
}

unsafe extern "C" fn expect_session(token: u32, sequence: u32) -> i32 {
    WIRE.with_borrow_mut(|w| {
        assert!(w.owned);
        assert_eq!(sequence, 1);
        w.arms += 1;
        w.expected = token;
        w.arm_error
    })
}
fn bootstrap_owner() -> Coordinator<'static, Link> {
    let mut c = admission_owner();
    c.request_link(0, 100).unwrap();
    assert_eq!(c.tick_link(1, true), LinkProgress::Owned);
    c
}
#[test]
fn session_exchange_arms_once_drains_each_write_and_keeps_sequence_namespace() {
    let mut c = bootstrap_owner();
    unsafe { c.begin_session(expect_session, 0xdeadbeef, 2, 100) }.unwrap();
    assert_eq!(c.request_link_release(3, 100), Err(Error::Busy));
    assert_eq!(
        unsafe { c.begin_session(expect_session, 1, 3, 100) },
        Err(Error::Busy)
    );
    for now in 3..9 {
        assert_eq!(c.tick_bootstrap(now), Progress::Pending);
    }
    assert!(!c.session_bound()); // transport completion alone is insufficient
    assert_eq!(c.tick_bootstrap(9), Progress::Idle);
    assert!(c.session_bound());
    assert_eq!(c.mailbox.reserve_session_sequence(), Ok(2));
    WIRE.with_borrow(|w| {
        assert_eq!(w.arms, 1);
        assert_eq!(w.starts, 3);
        assert_eq!(w.operations, [(1, 5, 0)]);
        assert!(w.source.is_none());
    });
    assert_eq!(
        unsafe { c.begin_session(expect_session, 2, 10, 100) },
        Err(Error::Busy)
    );
    c.request_link_release(10, 100).unwrap();
    assert_eq!(c.tick_link(11, true), LinkProgress::Released);
}
#[test]
fn session_deadline_cannot_be_renewed_and_retains_pending_buffer() {
    for mode in [0, 1] {
        let mut c = bootstrap_owner();
        unsafe { c.begin_session(expect_session, 1, u32::MAX - 2, 5) }.unwrap();
        WIRE.with_borrow_mut(|w| {
            w.mode = mode;
            w.busy = mode == 1;
        });
        assert_eq!(c.tick(u32::MAX - 1), Progress::Pending);
        assert_eq!(c.tick(1), Progress::Pending);
        assert_eq!(c.tick(2), Progress::Blocked(Failure::Timeout));
        let counts = WIRE.with_borrow(|w| (w.starts, w.polls, w.snapshots));
        assert_eq!(c.tick(3), Progress::Blocked(Failure::Timeout));
        assert_eq!(c.request_link_release(3, 10), Err(Error::Blocked));
        WIRE.with_borrow(|w| {
            assert_eq!((w.starts, w.polls, w.snapshots), counts);
            assert!(w.owned);
            if mode == 0 {
                assert!(w.source.is_some());
            }
        });
    }
}
#[test]
fn session_ack_loss_and_wrong_identity_block_without_rebind() {
    for silent in [false, true] {
        let mut c = bootstrap_owner();
        unsafe { c.begin_session(expect_session, 7, 2, 10) }.unwrap();
        for now in 3..9 {
            assert_eq!(c.tick(now), Progress::Pending);
        }
        WIRE.with_borrow_mut(|w| {
            w.silent = silent;
            w.reply[12] ^= 1;
            let (mut a, mut b) = (0u32, 0u32);
            for word in &w.reply[..15] {
                a = a.wrapping_add(*word);
                b = b.wrapping_add(a);
            }
            w.reply[15] = a ^ b;
        });
        if silent {
            assert_eq!(c.tick(9), Progress::Pending);
            assert_eq!(
                c.tick_link(12, true),
                LinkProgress::Blocked(Failure::Timeout)
            );
        } else {
            assert_eq!(c.tick(9), Progress::Blocked(Failure::Reply));
        }
        assert!(!c.session_bound());
        assert_eq!(
            unsafe { c.begin_session(expect_session, 8, 13, 10) },
            Err(Error::Blocked)
        );
        WIRE.with_borrow(|w| {
            assert_eq!(w.arms, 1);
            assert_eq!(w.starts, 3);
            assert_eq!(w.releases, 0);
        });
    }
}
#[test]
fn session_arm_failure_and_boot_loss_never_send_or_release() {
    for error in [-1, -2, 7] {
        let mut c = bootstrap_owner();
        WIRE.with_borrow_mut(|w| w.arm_error = error);
        assert_eq!(
            unsafe { c.begin_session(expect_session, 1, 2, 10) },
            Err(Error::Blocked)
        );
        assert!(!c.session_bound());
        WIRE.with_borrow(|w| {
            assert_eq!(w.arms, 1);
            assert_eq!(w.starts, 0);
            assert!(w.owned);
        });
    }
    let mut c = bootstrap_owner();
    unsafe { c.begin_session(expect_session, 1, 2, 10) }.unwrap();
    WIRE.with_borrow_mut(|w| w.boot_lost = true);
    assert_eq!(c.tick_bootstrap(3), Progress::Blocked(Failure::Boot));
    WIRE.with_borrow(|w| {
        assert_eq!(w.starts, 0);
        assert_eq!(w.releases, 0);
    });
}
#[test]
fn session_requires_ownership_and_valid_budget_before_arming() {
    let mut c = admission_owner();
    assert_eq!(
        unsafe { c.begin_session(expect_session, 1, 0, 10) },
        Err(Error::NotOwned)
    );
    for (token, budget) in [(0, 10), (1, 0), (1, 0x80000000)] {
        assert_eq!(
            unsafe { c.begin_session(expect_session, token, 0, budget) },
            Err(Error::Budget)
        );
    }
    WIRE.with_borrow(|w| {
        assert_eq!(w.arms, 0);
        assert_eq!(w.starts, 0);
    });
}

#[test]
fn session_link_failure_retains_lease_and_never_retries_commit() {
    for mode in [2, 3] {
        let mut c = bootstrap_owner();
        unsafe { c.begin_session(expect_session, 1, 2, 100) }.unwrap();
        assert_eq!(c.tick(3), Progress::Pending);
        WIRE.with_borrow_mut(|w| w.mode = mode);
        assert_eq!(c.tick(4), Progress::Blocked(Failure::Link));
        assert_eq!(c.tick(5), Progress::Blocked(Failure::Link));
        WIRE.with_borrow(|w| {
            assert_eq!(w.starts, 1);
            assert_eq!(w.polls, 1);
            assert!(w.owned);
            assert_eq!(w.releases, 0);
        });
    }
}

#[test]
fn bound_session_loss_during_upload_retains_memory_and_pending_source() {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = bootstrap_owner();
    unsafe { owner.begin_session(expect_session, 17, 2, 100) }.unwrap();
    for now in 3..9 {
        assert_eq!(owner.tick(now), Progress::Pending);
    }
    assert_eq!(owner.tick(9), Progress::Idle);
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let output = Box::leak(Box::new([0; 64]));
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let plan = owner.admit(0, &section, &[20], &admission_limits(),
        &mut ledger, output, |_| Some(0xb80000)).unwrap();
    let ticket = plan.ticket();
    owner.begin(plan, output, 10, 100).unwrap();
    assert_eq!(owner.tick(11), Progress::Pending);
    WIRE.with_borrow_mut(|w| {
        assert!(w.source.is_some());
        w.boot_lost = true;
    });
    assert_eq!(owner.tick(12), Progress::Blocked(Failure::Boot));
    assert!(!owner.session_bound());
    // A formerly valid bind ACK cannot restart a revoked owner.
    WIRE.with_borrow_mut(|w| w.boot_lost = false);
    let counts = WIRE.with_borrow(|w| (w.starts, w.polls, w.snapshots));
    assert_eq!(owner.tick(13), Progress::Blocked(Failure::Boot));
    assert!(owner.release_memory(ticket, &mut ledger).is_err());
    assert_eq!(ledger.lengths[0], 128);
    assert_eq!(owner.loads().lookup(20), None);
    assert_eq!(owner.request_link_release(14, 100), Err(Error::Blocked));
    WIRE.with_borrow(|w| {
        assert_eq!((w.starts, w.polls, w.snapshots), counts);
        let (_, _, pointer, count) = w.source.unwrap();
        assert_eq!(unsafe { core::slice::from_raw_parts(pointer, count) }, output);
        assert_eq!(w.operations, [(1, 5, 0)]);
        assert_eq!(w.releases, 0);
        assert!(w.owned);
    });
}


fn published_session_owner() -> (Coordinator<'static, Link>, Ticket, [u32; 1], [u32; 1]) {
    use crate::instrument_section::{tests as section_test, Section};
    let mut owner = bootstrap_owner();
    unsafe { owner.begin_session(expect_session, 0x10203040, 2, 100) }.unwrap();
    for now in 3..10 { owner.tick(now); }
    assert!(owner.session_bound());
    let bytes = section_test::fixture();
    let section = Section::parse(&bytes, 1, section_test::limits()).unwrap();
    let output = Box::leak(Box::new([0; 64]));
    let mut offsets = [0];
    let mut lengths = [0];
    let mut ledger = Ledger { base: 0x20010000, bytes: 512,
        offsets: &mut offsets, lengths: &mut lengths };
    let plan = owner.admit(0, &section, &[20], &admission_limits(),
        &mut ledger, output, |_| Some(0xb80000)).unwrap();
    let ticket = plan.ticket();
    owner.begin(plan, output, 10, 100).unwrap();
    for now in 11..100 {
        if owner.tick(now) == (Progress::Complete {
            command: Command::Publish(ticket), recovered: None }) {
            return (owner, ticket, offsets, lengths);
        }
    }
    panic!("load did not publish");
}

#[test]
fn published_retirement_retains_memory_until_exact_detach_ack() {
    let (mut owner, ticket, mut offsets, mut lengths) = published_session_owner();
    let mut ledger = Ledger { base: 0x20010000, bytes: 512,
        offsets: &mut offsets, lengths: &mut lengths };
    assert_eq!(owner.retire(ticket, 100, 0), Err(Error::Budget));
    assert!(owner.loads().lookup(20).is_some());
    owner.retire(ticket, 100, 100).unwrap();
    assert!(owner.loads().lookup(20).is_none());
    assert_eq!(owner.retire(ticket, 101, 100), Err(Error::Busy));
    assert_eq!(owner.request_link_release(101, 100), Err(Error::Busy));
    assert!(owner.release_memory(ticket, &mut ledger).is_err());
    assert_eq!(ledger.lengths[0], 128);
    assert!(owner.process_reserved() > 0);
    for now in 101..200 {
        match owner.tick(now) {
            Progress::Pending => assert_eq!(ledger.lengths[0], 128),
            Progress::Complete { command: Command::Release(done), recovered: None } => {
                assert_eq!(done, ticket);
                owner.release_memory(ticket, &mut ledger).unwrap();
                assert_eq!(ledger.lengths[0], 0);
                assert_eq!(owner.process_reserved(), 0);
                assert!(matches!(owner.retire(ticket, now, 100), Err(Error::Load(_))));
                owner.request_link_release(now, 100).unwrap();
                assert_eq!(owner.tick_link(now + 1, true), LinkProgress::Released);
                WIRE.with_borrow(|w| assert_eq!(w.operations.iter().map(|x| (x.0, x.1)).collect::<Vec<_>>(),
                    [(1, 5), (2, 4), (3, 2), (4, 3)]));
                return;
            }
            other => panic!("unexpected retirement: {other:?}"),
        }
    }
    panic!("retirement did not finish");
}

#[test]
fn retirement_failure_cannot_release_or_retry_live_storage() {
    for loss in 0..3 {
        let (mut owner, ticket, mut offsets, mut lengths) = published_session_owner();
        let mut ledger = Ledger { base: 0x20010000, bytes: 512,
            offsets: &mut offsets, lengths: &mut lengths };
        owner.retire(ticket, 100, 20).unwrap();
        WIRE.with_borrow_mut(|w| match loss {
            0 => w.reject = 3,
            1 => w.silent = true,
            _ => w.boot_lost = true,
        });
        let mut blocked = false;
        for now in 101..150 {
            if matches!(owner.tick(now), Progress::Blocked(_)) { blocked = true; break; }
        }
        assert!(blocked);
        assert_eq!(owner.retire(ticket, 150, 20), Err(Error::Blocked));
        assert_eq!(owner.request_link_release(150, 20), Err(Error::Blocked));
        assert!(owner.release_memory(ticket, &mut ledger).is_err());
        assert_eq!(ledger.lengths[0], 128);
        assert!(owner.process_reserved() > 0);
        assert!(owner.loads().lookup(20).is_none());
        WIRE.with_borrow_mut(|w| { w.reject = 0; w.silent = false; w.boot_lost = false; });
        assert!(matches!(owner.tick(151), Progress::Blocked(_)));
    }
}
