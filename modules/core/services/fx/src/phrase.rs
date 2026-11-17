//! Optional exclusive phrase FX capability. The import, never a module name,
//! selects the route. Claiming it twice fails before allocating or publishing.
//! It is a part of the fx service: the phrase effect is one effect row whose route, mix and page the image's phrase
//! adapters own (core/phrase-control, core/phrase-ui, core/phrase-route). The service's DSP hooks call it (`records`).
use core::ptr::addr_of_mut;
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::dsp::{DspSec, Job, DSP_IMPORT_LEN, I_IMPORTS, I_NIMPORT};
use dkrt_abi::mem::{cstr_eq, iw};
use dkrt_sdk::*;

/// The DSP import that makes a module's one effect the phrase effect.
const CONTROL: &[u8] = b"_phrase_control_info\0";
/// The routing provider's DSP symbol: without it the image cannot route a phrase effect.
const ROUTE: &[u8] = b"_phrase_route_info\0";
extern "C" {
    /// Optional symbols of core/phrase-ui and core/phrase-control: each resolves to 0 when its module is not in the image
    /// (`[object] optional` of the runtime). A host test binary defines them (`tests/phrase_stubs.rs`).
    fn phrase_bind(owner: u32, row: u32, page: u32, module: *const u8, title: *const u8);
    fn phrase_control_tick();
    fn phrase_ui_tick();
}
#[derive(Clone, Copy)]
struct Owner {
    module: u8,
    row: usize,
}
impl Owner {
    const EMPTY: Self = Self { module: 0, row: 0 };

    fn free(&self) -> bool {
        self.module == 0
    }

    fn is_row(&self, row: usize) -> bool {
        !self.free() && self.row == row
    }

    fn bind(&self, row: usize, publish: impl FnOnce(u8)) {
        if self.is_row(row) {
            publish(self.module);
        }
    }

    fn remove(&mut self, module: u8, revoke: impl FnOnce(&Self)) {
        if !self.free() && self.module == module {
            // Keep the owner live until the native adapter has revoked its
            // control page/route. A new module may claim the slot afterward.
            revoke(self);
            *self = Self::EMPTY;
        }
    }
}
static mut OWNER: Owner = Owner::EMPTY;
fn owner() -> &'static mut Owner {
    unsafe { &mut *addr_of_mut!(OWNER) }
}
pub unsafe fn available() -> bool {
    core::hint::black_box(phrase_bind as usize) != 0
}
pub fn free() -> bool {
    owner().free()
}
pub fn is_row(row: usize) -> bool {
    owner().is_row(row)
}
pub fn reserve(module: u8, row: usize) {
    *owner() = Owner { module, row };
}
pub unsafe fn bind<C: CoreApi>(core: &C, row: usize, physical: u32, page: u32, title: *const u8) {
    let o = *owner();
    o.bind(row, |module| {
        if available() {
            phrase_bind(
                module as u32,
                physical,
                page,
                core.module_name(module),
                title,
            );
        }
    });
}
pub unsafe fn remove(module: u8) {
    owner().remove(module, |_| {
        if available() {
            phrase_bind(0, 0, 0, core::ptr::null(), core::ptr::null());
        }
    });
}
/// Whether the import table `imports` (`n` entries of `DSP_IMPORT_LEN` bytes, a name first) names the phrase control.
unsafe fn imports_control(imports: *const u8, n: u32) -> bool {
    let mut i = 0;
    while i < n {
        if cstr_eq(
            imports.add(DSP_IMPORT_LEN as usize * i as usize),
            CONTROL.as_ptr(),
        ) {
            return true;
        }
        i += 1;
    }
    false
}

/// Whether the image exports the DSP symbol `name` (the import table of `dyndsp_info`).
unsafe fn image_exports<C: CoreApi>(core: &C, name: *const u8) -> bool {
    let inf = core.dsp_info();
    if inf.is_null() {
        return false;
    }
    let n = iw(inf, I_NIMPORT);
    let mut i = 0;
    while i < n {
        if cstr_eq(
            (inf.add(I_IMPORTS) as *const u8).add(DSP_IMPORT_LEN as usize * i as usize),
            name,
        ) {
            return true;
        }
        i += 1;
    }
    false
}

/// The section check: a module that imports the phrase control is one effect and no voice, in an image with both phrase providers.
pub unsafe fn check<C: CoreApi>(core: &C, sec: &DspSec) -> i32 {
    if !imports_control(sec.s.add(sec.import_off as usize), sec.nimport) {
        return OK;
    }
    if sec.nfx != 1 || sec.nvoice != 0 || !available() || !image_exports(core, ROUTE.as_ptr()) {
        return E_DSP_IMPORT;
    }
    OK
}

/// Whether `job` is a phrase effect (its import table was checked by `check`).
pub unsafe fn wanted(job: &Job) -> bool {
    imports_control(job.imports, job.nimport)
}

/// Before anything is reserved: a second phrase effect is refused.
pub unsafe fn admit(job: &Job) -> i32 {
    if wanted(job) && !free() {
        return E_BUSY;
    }
    OK
}

pub unsafe fn tick() {
    if core::hint::black_box(phrase_control_tick as usize) != 0 {
        phrase_control_tick();
    }
    if core::hint::black_box(phrase_ui_tick as usize) != 0 {
        phrase_ui_tick();
    }
}

#[cfg(test)]
mod tests {
    use super::Owner;

    include!("../tests/phrase_stubs.rs");

    // Local owner instances exercise the same transition code as the runtime
    // without racing unrelated loader tests' global module/transport fixtures.
    // The DSP loader checks free() before allocating the second phrase module;
    // these tests cover the lifetime of that exclusive claim, not DSP ACKs.
    #[test]
    fn empty_owner_cannot_publish_or_revoke_a_route() {
        let mut o = Owner::EMPTY;
        assert!(o.free());
        assert!(!o.is_row(0));
        o.bind(0, |_| panic!("empty owner published"));
        o.remove(0, |_| panic!("empty owner revoked"));
        o.remove(1, |_| panic!("unrelated owner revoked"));
        assert!(o.free());
    }

    #[test]
    fn a_claim_locks_only_its_own_fx_row() {
        let o = Owner { module: 3, row: 2 };
        assert!(!o.free()); // the loader must reject a second phrase claim
        assert!(o.is_row(2));
        assert!(!o.is_row(0));
        assert!(!o.is_row(3));
        let mut published = None;
        o.bind(2, |module| published = Some(module));
        assert_eq!(published, Some(3));
        o.bind(1, |_| panic!("another FX row published the phrase route"));
    }

    #[test]
    fn removing_another_module_keeps_the_route_and_claim() {
        let mut o = Owner { module: 2, row: 1 };
        o.remove(1, |_| panic!("unrelated module revoked the route"));
        o.remove(0, |_| panic!("zero owner revoked the route"));
        assert!(!o.free());
        assert!(o.is_row(1));
        let mut module = 0;
        o.bind(1, |owner| module = owner);
        assert_eq!(module, 2);
    }

    #[test]
    fn unload_revokes_before_freeing_and_repeated_cleanup_is_inert() {
        let mut o = Owner { module: 2, row: 4 };
        let mut revocations = 0;
        o.remove(2, |held| {
            assert!(!held.free());
            assert_eq!(held.module, 2);
            assert!(held.is_row(4));
            revocations += 1;
        });
        assert_eq!(revocations, 1);
        assert!(o.free());
        assert!(!o.is_row(4));
        o.bind(4, |_| panic!("unloaded route republished"));
        o.remove(2, |_| panic!("FX page and DSP cleanup revoked twice"));
    }

    #[test]
    fn a_new_module_can_bind_after_the_previous_owner_unloads() {
        let mut o = Owner { module: 1, row: 0 };
        o.remove(1, |_| {});
        assert!(o.free());
        o = Owner { module: 5, row: 3 };
        let mut published = 0;
        o.bind(3, |module| published = module);
        assert_eq!(published, 5);
        o.bind(0, |_| panic!("previous owner's row remained bound"));
        o.remove(1, |_| panic!("previous owner revoked the replacement"));
        assert!(o.is_row(3));
        o.remove(5, |_| {});
        assert!(o.free());
    }
}
