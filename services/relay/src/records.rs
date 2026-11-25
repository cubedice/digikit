//! `relay` service, records: none. The service owns no record kind, counts no capacity and has no part of its own: its owner (`RelayFile` of the service
//! file, `dks/src/lib.rs`) only binds the image table at its start. RELAY's machine is a card module that needs this file; its tapes are files of
//! the +Drive, which the moved code reads and writes on the runtime's I/O task.

/// The record kinds the service owns: none.
pub const KINDS: [(u32, u32); 0] = [];
