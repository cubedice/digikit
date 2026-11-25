//! `Views`: pointers to the resident statics the package reads and writes. The resident core owns the statics (host tools peek them by symbol); the package holds the
//! pointers. The layout of what each pointer reaches is part of the major version of the table (`resident.rs`, "Shared views"): `PackageState` (with `Module` and
//! `LogEntry`), `DkrtBootInfo` (generated from `runtime/core/abi.toml`), the name array and the `MAX_MODULES` arrays.
use crate::runtime_state::PackageState;
use crate::state::NAME_LEN;
use crate::MAX_MODULES;
use dkrt_sdk::{DkrtBootInfo, SEL_MAX};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Views {
    /// `DKRT_STATE`.
    pub state: *mut PackageState,
    /// `DKRT_BOOT`.
    pub boot_info: *mut DkrtBootInfo,
    /// The names of the boot's modules (`LOADS` reports them).
    pub boot_names: *const [[u8; NAME_LEN]; SEL_MAX],
    /// `DKRT_PATHS`: the store path each module record was loaded from.
    pub paths: *mut [[u8; 64]; MAX_MODULES],
    /// `DKRT_MENUS`: the address of each record's `menu`.
    pub menus: *mut [u32; MAX_MODULES],
    /// `DKRT_MENU_OBJS`: whether a menu object was built for each record.
    pub menu_objs: *mut [u8; MAX_MODULES],
    /// `DKRT_MODULE_RUNTIME`: the runtime version each record needs.
    pub module_runtime: *mut [u32; MAX_MODULES],
    /// The resident statics of the fork's persistent V2 instrument host (appended, minor 5): the image adapters read `dkrt_instrument_type` and
    /// `dkrt_instrument_generation` (from `words`) and the tests read the diagnostics by symbol; the package's coordinator writes them.
    pub instrument: InstrumentViews,
}

/// `DKRT_INSTRUMENT_WORDS` (type and generation of the published instrument, 0 while none), `DKRT_INSTRUMENT_FILE`, `DKRT_INSTRUMENT_SESSION`,
/// `DKRT_INSTRUMENT_LOAD` and `DKRT_INSTRUMENT_BINDING`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct InstrumentViews {
    pub words: *mut [u32; 2],
    pub file: *mut [u32; 4],
    pub session: *mut [u32; 3],
    pub load: *mut [u32; 6],
    pub binding: *mut dkrt_sdk::instrument_provider::Binding,
}

// The pointers name resident statics for the whole run of the system; the views are built once and only read.
unsafe impl Sync for Views {}
