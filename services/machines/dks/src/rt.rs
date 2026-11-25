//! The exports only the runtime calls, by name (`internal = true`, `wrapper = "rt"` in sdk/sdk.toml; in no header and no module's import table): the V2 instrument loader's
//! two queries of the machine type numbers. They reach no owner (`Machines::planned_ids` and `Machines::id_taken` read the tables the service keeps by name), so they answer the
//! same with or without one, and inside another call of the owner.
use dkrt_svc_machines::machines::Machines;
use dkrt_svc_rt::ImportCore;

/// The plan's `[key, number]` pairs of the module at selection index `selection` into the 8 pairs at `out`; how many.
#[cfg_attr(target_arch = "m68k", no_mangle)]
pub unsafe extern "C" fn dk_machines_planned_ids(selection: u32, out: *mut u32) -> u32 {
    Machines::planned_ids(selection, &mut *(out as *mut [[u32; 2]; 8]))
}

/// 1 when machine type `id` is taken in the machine rows (or the service cannot place it), else 0.
#[cfg_attr(target_arch = "m68k", no_mangle)]
pub unsafe extern "C" fn dk_machines_id_taken(id: u32) -> u32 {
    Machines::id_taken(&ImportCore, id) as u32
}
