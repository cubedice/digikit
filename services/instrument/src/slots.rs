//! `instrument` service, slots: the `[[slot]]` words of the image adapters that the service file fills (`dks/module.toml`
//! `[[fill]]`). Each holds the address of a vector table of the file (`dks/src/seam.s`, generated with the image's forwarders from
//! `seam.toml`); a forwarder reads the word and jumps through entry i, or takes the stock path while it is 0. The file takes no hook.

/// The slot of `core/instrument-play` and the table the file fills it with.
pub const INSTRUMENT: (&str, &str) = ("instrument_vec", "instrument_vec_table");
/// The slot of `core/midi-events` and the table the file fills it with.
pub const MIDI: (&str, &str) = ("midi_vec", "midi_vec_table");
