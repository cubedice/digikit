//! `relay` service, slots: the `[[slot]]` word of `core/relay-track` that the service file fills (`dks/module.toml` `[[fill]]`). It holds
//! the address of the vector table of the file (`dks/src/seam.s`, generated with the image's forwarders from `seam.toml`); a forwarder
//! reads the word and jumps through entry i, or takes the path without RELAY while it is 0. The file takes no hook.

/// The slot of `core/relay-track` and the table the file fills it with.
pub const RELAY: (&str, &str) = ("relay_vec", "relay_vec_table");
