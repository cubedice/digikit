//! Host test of the fixture's owner over `FakeCore` (the layout of a service folder has one; nothing runs it): it starts and declares nothing.
use dkrt_abi::provider::{Declarations, Provider};
use dkrt_svc_svc_test::svc_test::SvcTest;
use dkrt_test_support::FakeCore;

#[test]
fn the_owner_starts_and_declares_nothing() {
    let core = FakeCore::new();
    let owner = <SvcTest as Provider<FakeCore>>::start(&core).expect("start");
    let mut d = Declarations::new();
    <SvcTest as Provider<FakeCore>>::declare(&owner, &mut d);
    assert!(d.resources().is_empty() && d.entries().is_empty());
}
