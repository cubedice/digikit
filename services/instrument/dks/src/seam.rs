//! The file's side of the seam: the two vector tables the fills name (assembler of `seam.s`) and `setup`, which binds the image
//! table into `ip_img` (the bss words of `seam.s` that the thunks and the C code read) with the check of `dkrt_svc_instrument::seam`.
use dkrt_abi::core_api::CoreApi;
use dkrt_abi::provider::{Provider, Status};
use dkrt_sdk::{E_NO_REGISTRY, OK};
use dkrt_svc_instrument::seam::{usable, INFO};
use dkrt_svc_rt::name_hash;

macro_rules! entries {
    ($($name:ident,)*) => {
        $(
            #[cfg(target_arch = "m68k")]
            extern "C" {
                pub fn $name();
            }
            #[cfg(not(target_arch = "m68k"))]
            pub extern "C" fn $name() {}
        )*
    };
}

// the vector tables of `seam.s` (data, named as functions so the fills can take their address)
entries! {
    instrument_vec_table,
    midi_vec_table,
    kbp_vec_table,
}

#[cfg(target_arch = "m68k")]
extern "C" {
    /// The copy of the image table (`seam.s`, bss).
    static mut ip_img: [u32; dkrt_svc_instrument::seam::WORDS];
    /// Fill the registry codes of src-mod.c from `ip_img`.
    fn ip_seam_rows();
}

/// The service file's `setup` (the first step of `init`): bind the image table, or refuse the file (`E_NO_REGISTRY`: the image has
/// no core/instrument-play of this layout). The loader fills the slots after `init`, so no forwarder reaches the moved code
/// before this has run.
///
/// # Safety
/// Called once by `init`.
pub unsafe fn setup<C: CoreApi>(core: &C) -> i32 {
    let src = core.info(name_hash(INFO));
    if !usable(src) {
        return E_NO_REGISTRY;
    }
    #[cfg(target_arch = "m68k")]
    {
        dkrt_svc_instrument::seam::copy(src, core::ptr::addr_of_mut!(ip_img) as *mut u32);
        ip_seam_rows();
    }
    OK
}

/// The owner of the file: it holds nothing; its `start` binds the image table (`seam::setup`: refused, `E_NO_REGISTRY`, without a usable one), which
/// the loader runs before it fills the slots, so no forwarder reaches the moved code before the table is bound.
pub struct InstrumentFile;

impl<C: CoreApi> Provider<C> for InstrumentFile {
    fn start(core: &C) -> Result<Self, Status> {
        // SAFETY: `start` runs once, in the file's `init`
        match unsafe { setup(core) } {
            dkrt_sdk::OK => Ok(InstrumentFile),
            e => Err(e),
        }
    }
}
