//! What the loader needs from outside, as two traits (docs/plans/runtime-and-services.md section 13.2, R12-3a). The real types call the store, the arena, the
//! module entry and the registry; the fakes of the host tests log the calls. Each trait has one real implementation, so there is no `dyn`.

use crate::entry::Ctx;
use dkrt_abi::service::{ModuleCommitCtx, ServiceRow};

/// A staged file: `size` bytes at `ptr`, valid until the next `stage`.
#[derive(Clone, Copy)]
pub struct Staged {
    pub ptr: *const u8,
    pub size: u32,
}

/// An arena block: the host pointer to write through, and the address the module sees (the same number on the device).
#[derive(Clone, Copy)]
pub struct Block {
    pub ptr: *mut u8,
    pub addr: u32,
}

/// A file of persistent V2 instruments after the loader placed it, handed to their owner (`Images::queue_instruments`). The addresses are in the
/// module's arena block: the copies of the keys table and of the DSP section, and the output buffer of the relocated DSP image.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct InstrumentLoad {
    pub rec: usize,
    /// The selection index the file was loaded for.
    pub selection: u32,
    pub image_size: u32,
    pub keys: u32,
    pub keys_size: u32,
    pub section: u32,
    pub section_size: u32,
    pub output: u32,
    pub output_size: u32,
    /// The addresses of `init`, `tick` and `menu` (0: none); the owner publishes them with the CF side (`Loader::publish_v2`).
    pub init: u32,
    pub tick: u32,
    pub menu: u32,
}

/// Where a module's bytes come from and where they go: the file store, the runtime's import table, the arena, the calls into module code and the owner
/// of persistent V2 instruments.
pub trait Images {
    /// Read the file at the NUL-terminated store `path` into the staging buffer. Errors are the loader's result codes (`E_NOT_FOUND`, `E_SHA`, ...).
    unsafe fn stage(&mut self, path: *const u8) -> Result<Staged, i32>;
    /// A block of at least `n` bytes; `None` when none fits.
    unsafe fn alloc(&mut self, n: u32) -> Option<Block>;
    unsafe fn free(&mut self, addr: u32);
    /// The address of the import `name` (NUL-terminated), 0 when the runtime has none (`import_addr`).
    unsafe fn import_addr(&mut self, name: *const u8) -> u32;
    /// The address of the service-only import `name` (`SVC_IMPORTS`: the `dkc_*` of interface C, `dk_service_declare_row`, `dkc_panic`), 0 when there is none. The loader
    /// asks only for a file of role 1, after `import_addr`.
    unsafe fn svc_import_addr(&mut self, _name: *const u8) -> u32 {
        0
    }
    /// The module whose absence leaves the import `name` without an address (`import_needs`).
    unsafe fn import_needs(&mut self, name: *const u8) -> Option<&'static str>;
    /// The files of the store whose path starts with `prefix` (NUL-terminated), in path order, at most `max` into `out`: the count, or a negative code (`E_BUSY`).
    unsafe fn list(&mut self, prefix: *const u8, out: *mut dkrt_sdk::DkrtStoreEntry, max: u32) -> i32;
    /// Call `init` of module record `rec` (the address `init` in module memory) with its context; its result.
    unsafe fn enter_init(&mut self, rec: usize, init: u32, ctx: *const Ctx) -> i32;
    unsafe fn enter_fini(&mut self, rec: usize, fini: u32, ctx: *const Ctx);
    /// The owner of persistent V2 instruments (core/instrument-play) drains or owns the DSP link during a transaction: no load starts until this is
    /// true. Without an owner it always is.
    unsafe fn instruments_idle(&mut self) -> bool {
        true
    }
    /// Admit the version 7 or 8 DSP section of the staged file, for selection index `selection`: the bytes of the output buffer its relocated image
    /// takes, or the refusal (`E_CONTRACT`: no owner, or it has a load in flight; `E_FORMAT`: a section or machine keys it does not take).
    unsafe fn admit_instruments(&mut self, _section: &[u8], _selection: u32) -> Result<u32, i32> {
        Err(dkrt_sdk::E_CONTRACT)
    }
    /// Hand the placed load to the owner. It transfers the DSP side over the UI-loop passes and then publishes the CF side.
    unsafe fn queue_instruments(&mut self, _load: InstrumentLoad) {}
}

/// The registry of what a loaded file publishes (R12-4): the rows of a service file, and the rows of a module's records.
pub trait Registry {
    /// Does the runtime accept a service file with this ABI fingerprint (header word 0x3c)? 0: the file uses neither interface B nor C.
    fn abi_ok(&self, fingerprint: u32) -> bool;
    /// Publish the service row that `init` of record `rec` declared (`None`: it declared none, as every module today).
    unsafe fn add(&mut self, rec: usize, row: Option<&ServiceRow>) -> i32;
    /// Take everything record `rec` published down (idempotent).
    unsafe fn remove(&mut self, rec: usize);
    /// The `init` of record `rec` is about to run: every service forgets what an earlier module collected (`Service::module_begin`).
    unsafe fn module_begin(&mut self, rec: usize);
    /// The services' commit of record `rec` (`Service::module_commit`): the first refusal is the result.
    unsafe fn module_commit(&mut self, rec: usize, c: &ModuleCommitCtx) -> i32;
}

/// The store, the arena and the module entry of the device.
pub struct RealImages;

impl Images for RealImages {
    unsafe fn stage(&mut self, path: *const u8) -> Result<Staged, i32> {
        let found = crate::store::find(path)?;
        let r = crate::store::load_to_stage(&found);
        if r != dkrt_sdk::OK {
            return Err(r);
        }
        Ok(Staged { ptr: crate::store::stage() as *const u8, size: found.size })
    }
    unsafe fn list(&mut self, prefix: *const u8, out: *mut dkrt_sdk::DkrtStoreEntry, max: u32) -> i32 {
        crate::store::list(prefix, out, max)
    }
    unsafe fn alloc(&mut self, n: u32) -> Option<Block> {
        let p = crate::arena::alloc(n);
        if p.is_null() {
            None
        } else {
            Some(Block { ptr: p, addr: p as u32 })
        }
    }
    unsafe fn free(&mut self, addr: u32) {
        crate::arena::free(addr as *mut u8);
    }
    unsafe fn import_addr(&mut self, name: *const u8) -> u32 {
        crate::import_addr(name)
    }
    unsafe fn svc_import_addr(&mut self, name: *const u8) -> u32 {
        crate::svc_imports::svc_import_addr(name)
    }
    unsafe fn import_needs(&mut self, name: *const u8) -> Option<&'static str> {
        crate::import_needs(name)
    }
    unsafe fn enter_init(&mut self, rec: usize, init: u32, ctx: *const Ctx) -> i32 {
        let call: unsafe extern "C" fn(*const Ctx) -> i32 = core::mem::transmute(init as usize);
        crate::dlog::call_init(rec, call, ctx)
    }
    unsafe fn enter_fini(&mut self, rec: usize, fini: u32, ctx: *const Ctx) {
        #[cfg(test)]
        {
            let _ = ctx;
            crate::cleanup_tests::fini(rec, fini);
        }
        #[cfg(not(test))]
        {
            let call: unsafe extern "C" fn(*const Ctx) = core::mem::transmute(fini as usize);
            crate::dlog::call_fini(rec, call, ctx);
        }
    }
    unsafe fn instruments_idle(&mut self) -> bool {
        crate::instrument::legacy_policy() == dkrt_sdk::instrument_coordinator::LegacyPolicy::Run
    }
    unsafe fn admit_instruments(&mut self, section: &[u8], selection: u32) -> Result<u32, i32> {
        use crate::instrument::pending;
        // the admission policy comes from the firmware adapter only; one V2 file at a time
        if pending::policy().is_none() || pending::occupied() {
            return Err(dkrt_sdk::E_CONTRACT);
        }
        let Some(limits) = pending::descriptor_limits() else {
            return Err(dkrt_sdk::E_CONTRACT);
        };
        let Ok(section) = dkrt_sdk::instrument_section::Section::parse(section, crate::device(), limits) else {
            return Err(dkrt_sdk::E_FORMAT);
        };
        if crate::instrument::resolve_types(&section, selection).is_err() {
            return Err(dkrt_sdk::E_FORMAT);
        }
        Ok(section.memory_request().image_bytes)
    }
    unsafe fn queue_instruments(&mut self, l: InstrumentLoad) {
        use crate::instrument::pending;
        pending::queue(pending::Pending::new(
            l.rec,
            l.selection,
            l.image_size,
            l.keys,
            l.keys_size,
            l.section,
            l.section_size,
            l.output,
            l.output_size,
            l.init,
            l.tick,
            l.menu,
        ));
    }
}

/// The runtime's own registry: the machine, parameter and page rows of a module's records, and the service row of a service file (`svc_rows.rs`).
pub struct RealRegistry;

impl Registry for RealRegistry {
    fn abi_ok(&self, fingerprint: u32) -> bool {
        fingerprint == 0 || fingerprint == dkrt_abi::service::SVC_ABI
    }
    unsafe fn add(&mut self, rec: usize, row: Option<&ServiceRow>) -> i32 {
        match row {
            None => dkrt_sdk::OK,
            Some(row) => crate::svc_rows::add(rec, row),
        }
    }
    unsafe fn remove(&mut self, rec: usize) {
        // the registry rows of a module record first (the service's own `remove` runs over the table with its row in), then the service row
        if rec < crate::MAX_MODULES {
            #[cfg(test)]
            crate::cleanup_tests::registry_remove(rec as u8 + 1);
            #[cfg(not(test))]
            crate::registry::remove(rec as u8 + 1);
        }
        crate::svc_rows::remove(rec);
    }
    unsafe fn module_begin(&mut self, rec: usize) {
        crate::handlers::services().module_begin(rec)
    }
    unsafe fn module_commit(&mut self, rec: usize, c: &ModuleCommitCtx) -> i32 {
        crate::handlers::services().module_commit(rec, c)
    }
}
