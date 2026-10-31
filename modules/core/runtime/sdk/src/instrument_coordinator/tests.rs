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
        assert_eq!(f.owner.tick(0), Progress::Idle);
        assert_eq!(
            f.owner.loads.start(f.ticket),
            Ok(Command::SendImage(f.ticket))
        );
        WIRE.with_borrow(|w| assert_eq!((w.starts, w.polls, w.snapshots), (0, 0, 0)));
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
