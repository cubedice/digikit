//! The `track-keyboard` service as a service file (`services/track-keyboard.dks`): the C of `core/track-keyboard` that left the
//! image (docs/plans/fork-image-diet.md section 9.2), behind the glue of dkrt-svc-rt.
//!
//! It owns no record kind and takes no hook. The image adapter keeps every hook site, the pattern storage and the hook entries;
//! each of its calls into the moved code goes through a forwarder that reads the slot word `tk_vec`. This file fills it with its
//! vector table (`seam.s`, generated from `seam.toml` with the forwarders), so the image runs the moved code only while the file is
//! loaded and takes the stock path otherwise. `setup` binds the image table `tk_image` first (the moved code reaches the image
//! through it); an image without a usable table refuses the file.
// a host build (the assembly test of the runtime) has std; the file is `no_std` on the ColdFire
#![cfg_attr(target_arch = "m68k", no_std)]

use seam::TrackKeyboardFile;

mod seam;

dkrt_svc_rt::service!(
    TrackKeyboardFile,
    "track-keyboard",
    &[],
    parts = [],
    hooks = [],
    fills = [("tk_vec", seam::tk_vec_table, dt2),]
);
