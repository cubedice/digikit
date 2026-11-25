//! The `relay` service as a service file (`services/relay.dks`): the ColdFire C of `core/relay-track` that left the image
//! (docs/plans/upstream-merge-1124/README.md, "RELAY as a service file"), behind the glue of dkrt-svc-rt.
//!
//! It owns no record kind and takes no hook. The image adapter keeps RELAY's DSP objects, its parameter entries and the two idle
//! paths of the block interrupt; each call of the image into the moved code goes through a forwarder that reads the slot word
//! `relay_vec`. This file fills it with its vector table (`seam.s`, generated from `seam.toml` with the forwarders), so the image
//! runs RELAY only while the file is loaded and runs without RELAY otherwise. `setup` binds the image table `relay_image` first
//! (the moved code reaches the image through it); an image without a usable table refuses the file.
// a host build (the assembly test of the runtime) has std; the file is `no_std` on the ColdFire
#![cfg_attr(target_arch = "m68k", no_std)]

use seam::RelayFile;

mod seam;

dkrt_svc_rt::service!(
    RelayFile,
    "relay",
    &[],
    parts = [],
    hooks = [],
    fills = [("relay_vec", seam::relay_vec_table, dt2),]
);
