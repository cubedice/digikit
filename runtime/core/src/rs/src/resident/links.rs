//! `LinkRows`: the other image modules' functions, which only the resident object links by name (`[object] optional` of the module.toml: each symbol is 0 when its module is not
//! in the image). The package gets their addresses here, in the order of its `optional::table`.
use dkrt_abi::resident::{Group, InstrumentLinks, LinkRows};

// The system package's imports of other modules (`dk_ui_*`, `dk_input_*`, `dk_usb_bulk_*`, `dk_dsp_link_*`, `dk_sdhc_large_*`).
dkrt_sdk::optional_links!();

extern "C" {
    fn dl_stats_hook();
    fn dl_tick_cb_drop();
}

#[cfg(not(test))]
extern "C" {
    fn dl_send();
    fn dl_send_win();
    fn dl_poll();
    fn dyndsp_info();
    // the fork's persistent V2 instrument host (`[object] optional`: 0 when the module is not in the image)
    fn dl_claim();
    fn dl_release();
    fn dl_send_owned();
    fn dl_poll_tagged();
    fn mi_registry_reply();
    fn mi_registry_expect_session();
    fn instrument_session_probe();
    fn instrument_load_probe();
    fn instrument_runtime_policy();
    fn instrument_play_faulted();
    fn dk_app_instrument_settings_rows();
    fn dk_app_instrument_module_settings();
}

/// The address of a symbol only the device image has: a host test links none of them, so its row is null.
macro_rules! device_row {
    ($f:ident) => {{
        #[cfg(test)]
        {
            core::ptr::null()
        }
        #[cfg(not(test))]
        {
            $f as *const ()
        }
    }};
}

pub(super) static ROWS: Group<LinkRows> = Group::new(LinkRows {
    optional: optional_link::ADDRESSES,
    dl_send: device_row!(dl_send),
    dl_send_win: device_row!(dl_send_win),
    dl_poll: device_row!(dl_poll),
    dyndsp_info: device_row!(dyndsp_info),
    // the host tests give these two themselves (`package/cleanup_tests.rs`)
    dl_stats_hook: dl_stats_hook as *const (),
    dl_tick_cb_drop: dl_tick_cb_drop as *const (),
    instrument: InstrumentLinks {
        dl_claim: device_row!(dl_claim),
        dl_release: device_row!(dl_release),
        dl_send_owned: device_row!(dl_send_owned),
        dl_poll_tagged: device_row!(dl_poll_tagged),
        mi_registry_reply: device_row!(mi_registry_reply),
        mi_registry_expect_session: device_row!(mi_registry_expect_session),
        instrument_session_probe: device_row!(instrument_session_probe),
        instrument_load_probe: device_row!(instrument_load_probe),
        instrument_runtime_policy: device_row!(instrument_runtime_policy),
        instrument_play_faulted: device_row!(instrument_play_faulted),
        dk_app_instrument_settings_rows: device_row!(dk_app_instrument_settings_rows),
        dk_app_instrument_module_settings: device_row!(dk_app_instrument_module_settings),
    },
});
