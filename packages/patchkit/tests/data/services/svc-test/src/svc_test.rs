//! The owner of the `svc-test` service. It holds nothing and adopts no part (`parts = []` in `dks/src/lib.rs`): the hook and the two exports of the service file are
//! routines of the C and assembler sources of its object (`csrc/`), which keep their own count.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::provider::{Provider, Status};

/// The svc-test service.
pub struct SvcTest;

impl<C: CoreApi> Provider<C> for SvcTest {
    /// Nothing to set up: the C source's count starts at zero with the file's bss.
    fn start(_core: &C) -> Result<Self, Status> {
        Ok(SvcTest)
    }
}
