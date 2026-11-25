//! The file's side of the seam: the vector table the fill names (assembler of `seam.s`) and `setup`, which binds the image table
//! into `rt_img` (the bss words of `seam.s` that the thunks and the C code read) with the check of `dkrt_svc_relay::seam`.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::provider::{Provider, Status};
use dkrt_sdk::{E_NO_REGISTRY, OK};
use dkrt_svc_rt::name_hash;
use dkrt_svc_relay::seam::{usable, INFO};

// the vector table of `seam.s` (data, named as a function so the fill can take its address)
#[cfg(target_arch = "m68k")]
extern "C" {
    pub fn relay_vec_table();
    /// The copy of the image table (`seam.s`, bss).
    static mut rt_img: [u32; dkrt_svc_relay::seam::WORDS];
}
#[cfg(not(target_arch = "m68k"))]
pub extern "C" fn relay_vec_table() {}

/// The service file's `setup` (the first step of `init`): bind the image table, or refuse the file (`E_NO_REGISTRY`: the image has
/// no core/relay-track of this layout). The loader fills the slot after `init`, so no forwarder reaches the moved code before
/// this has run.
///
/// # Safety
/// Called once by `init`.
pub unsafe fn setup<C: CoreApi>(core: &C) -> i32 {
    let src = core.info(name_hash(INFO));
    if !usable(src) {
        return E_NO_REGISTRY;
    }
    #[cfg(target_arch = "m68k")]
    dkrt_svc_relay::seam::copy(src, core::ptr::addr_of_mut!(rt_img) as *mut u32);
    OK
}

/// The owner of the file: it holds nothing; its `start` binds the image table (`seam::setup`: refused, `E_NO_REGISTRY`, without a usable one), which
/// the loader runs before it fills the slots, so no forwarder reaches the moved code before the table is bound.
pub struct RelayFile;

impl<C: CoreApi> Provider<C> for RelayFile {
    fn start(core: &C) -> Result<Self, Status> {
        // SAFETY: `start` runs once, in the file's `init`
        match unsafe { setup(core) } {
            dkrt_sdk::OK => Ok(RelayFile),
            e => Err(e),
        }
    }
}
