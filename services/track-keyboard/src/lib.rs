//! `track-keyboard` service: the player of the track keyboard and arpeggiator (the TRIG and KEYS pages, the MIDI routes, capture,
//! the pool admission and its runtime), moved out of the image of `core/track-keyboard` (docs/plans/fork-image-diet.md section 9.2).
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
