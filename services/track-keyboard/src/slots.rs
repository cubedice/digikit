//! `track-keyboard` service, slots: the `[[slot]]` word of `core/track-keyboard` that the service file fills (`dks/module.toml`
//! `[[fill]]`). It holds the address of the vector table of the file (`dks/src/seam.s`, generated with the image's forwarders from
//! `seam.toml`); a forwarder reads the word and jumps through entry i, or takes the stock path while it is 0. The file takes no hook.

/// The slot of `core/track-keyboard` and the table the file fills it with.
pub const TRACK_KEYBOARD: (&str, &str) = ("tk_vec", "tk_vec_table");
