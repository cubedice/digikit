//! `svc-test` service, handlers (interface B): none of its own; the defaults of `dkrt_abi::service::Service` do nothing.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::service::{Declare, Service};

/// The svc-test service.
pub struct SvcTest;

impl<C: CoreApi> Service<C> for SvcTest {
    /// No record kind: sdk/sdk.toml gives none to `svc-test`.
    unsafe fn declare(_d: &mut Declare) {}
}
