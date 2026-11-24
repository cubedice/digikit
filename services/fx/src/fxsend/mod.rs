//! Send effects: the route of a run-time effect and its send amounts, one for each track (docs/findings/64-fx-send-page.md; the DSP side is finding 62).
//!
//! A digikit effect runs on the master chain or on a send bus. The route is a setting (`fx/route/<module id>`, one byte, 0 master, 1 send; step 2's store) and is read when the
//! effect is registered, at the boot. The effect's record says which routes are allowed and which is the default (`route`, `routes` of the DK_FX record; flags word of the
//! record): the default is used when the setting is not there or names a route the effect does not allow. SETTINGS > FX ROUTING (the fx service) writes the setting through
//! `dkrt_settings_put`, and lists the effects with `dkrt_fx`; the device limits the send effects (`[limits] send_slots`, `fxpages_info` word 39: the cells of the send page); an effect past the limit
//! stays on the master, and one that may not run on the master (a send-only effect) is then run by neither route (route word 2: on the master its wet signal would replace the
//! mix). A send effect has a cell on the send page (the second page of the track FX key, `core/fxpages`): one knob row (`params.fs<c>`) whose value is the halfword
//! `Sound[send_indices[c]]` of the current track's Sound object, so the firmware's own page code shows, edits and draws it (`FSH Send=40`). The row has the value class of the
//! kit's Delay rows (no p-lock) and no CC.
//!
//! The amount of the effect for track t is the byte `Sound_t[idx] >> 8` (0 .. 127). It is kept in the settings (`fx/send/<module id>`, 16 bytes) and in the DSP row (words 19 to
//! 22, finding 62), never in an Elektron file: the sound loader zeroes the index (Load Kit, a pattern's kit, a project: the hook of `core/fxpages` counts the loads) and the
//! saver does not write it. `tick` (every UI-loop pass):
//!
//! - after the loader counter changed and stayed still for `SETTLE` passes, and when the live kit object changed (a pattern's kit), the stored amounts are written back
//!   into the Sound arrays of the 16 tracks first (so the zeroes of a load are never taken as a turn),
//! - then each Sound array is read: a value that is not the one last written or seen is a turn of the knob: the effect's amount for that track follows (a put into the
//!   settings, written to the card 2 s after the last change, and the DSP words, live values on the link through window 2 like the effect pages' values).
//!
//! Everything runs in the UI task (the settings store is not for other tasks).
//!
//! The phrase effect (`crate::phrase`, PRISM) is the one exception. Its page is the image's, and so are its amounts: on a send bus it has no cell, it is not counted
//! in the send slots (CAPACITY `SEND_SLOTS`, and the routes FX ROUTING offers the other effects), and its amounts are what the phrase adapters give (`phrase::sends`, the
//! tracks the page selects), read every pass and sent like any effect's; they are not kept in `fx/send/<module id>`. An image without `phrase_sends` gives it a cell like
//! any send effect.
mod apply;
mod choices;
mod effect;
mod layout;
mod naming;
mod route;
mod state;
mod table;
#[cfg(test)]
mod tests;
mod tick;
pub use apply::*;
pub use choices::*;
pub use effect::*;
pub use layout::*;
pub use naming::*;
pub use route::*;
pub use state::*;
pub use table::*;
pub use tick::*;
