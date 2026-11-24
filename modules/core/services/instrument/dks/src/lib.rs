//! The `instrument` service as a service file (`services/instrument.dks`): the C of `core/instrument-play` and `core/midi-events`
//! that left the image (docs/plans/fork-image-diet.md section 9), behind the glue of dkrt-svc-rt.
//!
//! It owns no record kind and takes no hook. The image adapters keep every hook site, the Sound persistence path and the hook
//! entries; each of their calls into the moved code goes through a forwarder that reads a slot word (`instrument_vec`,
//! `midi_vec`). This file fills both with its vector tables (`seam.s`, generated from `seam.toml` with the forwarders), so the
//! image runs the moved code only while the file is loaded and takes the stock path otherwise. `setup` binds the image table
//! `instrument_image` first (the moved code reaches the image through it); an image without a usable table refuses the file.
// a host build (the assembly test of the runtime) has std; the file is `no_std` on the ColdFire
#![cfg_attr(target_arch = "m68k", no_std)]

use dkrt_svc_instrument::records::{Instrument, KINDS};

mod seam;

dkrt_svc_rt::service!(
    Instrument,
    "instrument",
    &KINDS,
    setup = seam::setup::<dkrt_svc_rt::ImportCore>,
    hooks = [],
    fills = [
        ("instrument_vec", seam::instrument_vec_table, dt2),
        ("midi_vec", seam::midi_vec_table, dt2),
        ("kbp_vec", seam::kbp_vec_table, dt2),
    ]
);
