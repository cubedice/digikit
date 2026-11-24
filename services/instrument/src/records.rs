//! `instrument` service, records: none. The service owns no record kind, counts no capacity and has no part of its own: its owner (`InstrumentFile` of the
//! service file, `dks/src/lib.rs`) only binds the image table at its start. CANOPY's persistent instruments are rows of the runtime's instrument registry (the V2 section of the .dkm); this file only runs the host code the image forwards to.

/// The record kinds the service owns: none.
pub const KINDS: [(u32, u32); 0] = [];
