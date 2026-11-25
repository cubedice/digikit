//! `track-keyboard` service, records: none. The service owns no record kind, counts no capacity and has no part of its own: its owner (`TrackKeyboardFile` of the
//! service file, `dks/src/lib.rs`) only binds the image table at its start. The track keyboard's patterns are stored by the image (the pattern storage of `core/track-keyboard`); the player polls through the image tick and a poll it adds itself.

/// The record kinds the service owns: none.
pub const KINDS: [(u32, u32); 0] = [];
