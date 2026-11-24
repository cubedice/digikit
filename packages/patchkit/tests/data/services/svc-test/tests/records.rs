//! Host test of the fixture's handlers over `FakeCore` (the layout of a service folder has one; nothing runs it).
use dkrt_abi::service::{Declare, Service};
use dkrt_svc_svc_test::records::SvcTest;
use dkrt_test_support::FakeCore;

#[test]
fn the_service_declares_no_record_kind() {
    let mut d = Declare::new();
    unsafe { <SvcTest as Service<FakeCore>>::declare(&mut d) };
    assert_eq!(d.n, 0);
}
