//! `relay` service: RELAY's track recorder on the ColdFire (the tape transport and its commands, the SRC pages and waveforms, the
//! tape files, the Sound association of tapes), moved out of the image of `core/relay-track`
//! (docs/plans/upstream-merge-1124/README.md, "RELAY as a service file").
//!
//! The code itself is C of the adapter, compiled into the service file (`dks/`). This crate holds what is Rust: the service type
//! ([`records`]: no record kinds), the slot the file fills ([`slots`]) and the check and copy of the image table the file binds at
//! setup ([`seam`]). `tests/` are host tests of the check.
#![no_std]
#[cfg(test)]
extern crate std;
pub mod records;
pub mod seam;
pub mod slots;
