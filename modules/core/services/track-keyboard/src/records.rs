//! `track-keyboard` service, records: none. The service owns no record kind, counts no capacity and has no work of its own in the
//! runtime's passes: every trait method keeps its default. The track keyboard's patterns are stored by the image (the pattern
//! storage of `core/track-keyboard`); the player polls through the image tick and a poll it adds itself.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::service::Service;

/// The service of `services/track-keyboard.dks`.
pub struct TrackKeyboard;

impl<C: CoreApi> Service<C> for TrackKeyboard {}

/// The record kinds the service owns: none.
pub const KINDS: [(u32, u32); 0] = [];
