//! `instrument` service, records: none. The service owns no record kind, counts no capacity and has no work of its own in the
//! runtime's passes: every trait method keeps its default. CANOPY's persistent instruments are rows of the runtime's instrument
//! registry (the V2 section of the .dkm); this file only runs the host code the image forwards to.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::service::Service;

/// The service of `services/instrument.dks`.
pub struct Instrument;

impl<C: CoreApi> Service<C> for Instrument {}

/// The record kinds the service owns: none.
pub const KINDS: [(u32, u32); 0] = [];
