//! Test fixture: the `svc-test` service in the layout of a service folder (`patchkit service-check`). It owns no record kind; its service file
//! (`dks/`) links the C and assembler sources of `csrc/` (packages/patchkit/tests/service_c_sources.rs).
#![no_std]
#[cfg(test)]
extern crate std;
pub mod records;
pub mod slots;
