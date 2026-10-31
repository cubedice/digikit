extern crate std;
use super::*;
use crate::{instrument_provider::OwnedProviders, instrument_transport::Driver};
use std::{boxed::Box, cell::RefCell};

struct State {
    claim_result: i32,
    release_result: i32,
    next: u32,
    calls: [u32; 4],
    lease: u32,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State {
    claim_result: 0, release_result: 0, next: 7, calls: [0; 4], lease: 0,
}); }
unsafe extern "C" fn claim(out: *mut u32) -> i32 {
    STATE.with_borrow_mut(|s| {
        s.calls[0] += 1;
        if s.claim_result == 0 {
            *out = s.next;
            s.lease = s.next;
        }
        s.claim_result
    })
}
unsafe extern "C" fn release(lease: u32) -> i32 {
    STATE.with_borrow_mut(|s| {
        s.calls[1] += 1;
        assert_eq!(lease, s.lease);
        if s.release_result == 0 {
            s.lease = 0;
        }
        s.release_result
    })
}
unsafe extern "C" fn send(
    lease: u32,
    window: u32,
    dst: u32,
    src: *const u8,
    n: u32,
    ticket: *mut u32,
) -> i32 {
    STATE.with_borrow_mut(|s| {
        s.calls[2] += 1;
        assert_eq!(lease, s.lease);
        assert_eq!((window, dst, n), (3, 0, 4));
        assert_eq!(core::slice::from_raw_parts(src, 4), b"abcd");
        *ticket = 42;
        0
    })
}
unsafe extern "C" fn poll(id: u32, out: *mut Status) -> i32 {
    STATE.with_borrow_mut(|s| {
        s.calls[3] += 1;
    });
    assert_eq!(id, 42);
    (*out).state = 2;
    1
}
unsafe extern "C" fn snapshot(_: *mut u32, _: u32) -> i32 {
    panic!("binding called snapshot");
}
fn api() -> OwnedApi {
    STATE.with_borrow_mut(|s| {
        *s = State {
            claim_result: 0,
            release_result: 0,
            next: 7,
            calls: [0; 4],
            lease: 0,
        }
    });
    unsafe { OwnedApi::new(claim, release, send, poll) }
}

#[test]
fn binding_requires_all_five_entries_without_acquiring() {
    let _ = api();
    for mask in 0..32 {
        let bound = unsafe {
            OwnedProviders::bind(
                (mask & 1 != 0).then_some(claim),
                (mask & 2 != 0).then_some(release),
                (mask & 4 != 0).then_some(send),
                (mask & 8 != 0).then_some(poll),
                (mask & 16 != 0).then_some(snapshot),
            )
        };
        assert_eq!(bound.is_some(), mask == 31);
    }
    STATE.with_borrow(|s| assert_eq!(s.calls, [0; 4]));
}

#[test]
fn owned_abi_carries_lease_and_request_identity_and_requires_explicit_release() {
    let mut api = api();
    assert!(!api.transaction_ready());
    assert_eq!(unsafe { api.start(3, 0, b"abcd".as_ptr(), 4) }, (-3, 0));
    assert_eq!(api.claim(), Ok(true));
    assert_eq!(api.claim(), Ok(true)); // idempotent without another provider call
    assert_eq!(unsafe { api.start(3, 0, b"abcd".as_ptr(), 4) }, (0, 42));
    assert_eq!(api.poll(42, &mut Status::default()), 1);
    assert!(api.transaction_ready()); // transfer completion is not release
    assert_eq!(api.release(), Ok(()));
    assert!(!api.transaction_ready());
    STATE.with_borrow_mut(|s| s.next = 8);
    assert_eq!(api.claim(), Ok(true));
    drop(api); // dropping must not release a later transaction accidentally
    STATE.with_borrow(|s| {
        assert_eq!(s.lease, 8);
        assert_eq!(s.calls, [2, 1, 1, 1]);
    });
}

#[test]
fn busy_claim_and_release_do_not_allow_unowned_or_premature_starts() {
    let mut api = api();
    STATE.with_borrow_mut(|s| s.claim_result = -1);
    assert_eq!(api.claim(), Ok(false));
    assert!(!api.transaction_ready());
    STATE.with_borrow_mut(|s| s.claim_result = 0);
    assert_eq!(api.claim(), Ok(true));
    STATE.with_borrow_mut(|s| s.release_result = -1);
    assert_eq!(api.release(), Err(Error::Busy));
    assert_eq!(api.claim(), Err(Error::Busy));
    assert!(!api.transaction_ready());
    assert_eq!(unsafe { api.start(3, 0, b"abcd".as_ptr(), 4) }, (-3, 0));
    STATE.with_borrow_mut(|s| s.release_result = 0);
    assert_eq!(api.release(), Ok(()));
    STATE.with_borrow(|s| assert_eq!(s.calls, [2, 2, 0, 0]));
}

#[test]
fn repeated_or_zero_lease_poison_owner_without_further_provider_calls() {
    for next in [0, 7] {
        let mut api = api();
        assert_eq!(api.claim(), Ok(true));
        assert_eq!(api.release(), Ok(()));
        STATE.with_borrow_mut(|s| s.next = next);
        assert_eq!(api.claim(), Err(Error::Uncertain));
        assert_eq!(api.claim(), Err(Error::Uncertain));
        assert_eq!(api.release(), Err(Error::Uncertain));
        assert_eq!(api.poll(42, &mut Status::default()), -1);
        assert_eq!(unsafe { api.start(3, 0, b"abcd".as_ptr(), 4) }, (-3, 0));
        STATE.with_borrow(|s| assert_eq!(s.calls, [2, 1, 0, 0]));
    }
}

#[test]
fn stale_release_and_boot_invalidation_never_retry_or_reclaim() {
    let mut driver = Driver::new(api(), Box::leak(Box::new([0; 256])));
    assert_eq!(driver.claim_link(), Ok(true));
    STATE.with_borrow_mut(|s| s.release_result = -2);
    assert_eq!(driver.release_link(), Err(Error::Uncertain));
    assert_eq!(driver.claim_link(), Err(Error::Uncertain));
    assert_eq!(driver.release_link(), Err(Error::Uncertain));
    STATE.with_borrow(|s| assert_eq!(s.calls, [1, 1, 0, 0]));
    let mut driver = Driver::new(api(), Box::leak(Box::new([0; 256])));
    assert_eq!(driver.claim_link(), Ok(true));
    driver.invalidate_boot();
    assert_eq!(driver.release_link(), Err(Error::Uncertain));
    STATE.with_borrow(|s| {
        assert_eq!(s.calls, [1, 0, 0, 0]);
        assert_eq!(s.lease, 7);
    });
}
