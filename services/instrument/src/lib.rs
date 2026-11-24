//! `instrument` service: the persistent instrument host of CANOPY and RELAY and its MIDI ownership, moved out of the image of
//! `core/instrument-play` and `core/midi-events` (docs/plans/fork-image-diet.md section 9).
//!
//! The code itself is C of the two adapters, compiled into the service file (`dks/`). This crate holds what is Rust: the service
//! type ([`records`]: no record kinds), the slots the file fills ([`slots`]) and the check and copy of the image table the file
//! binds at setup ([`seam`]). `tests/` are host tests of the check.
#![no_std]
#[cfg(test)]
extern crate std;
pub mod records;
pub mod seam;
pub mod slots;
