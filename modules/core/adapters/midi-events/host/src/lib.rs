//! Executes the production ColdFire C ownership implementation on the host.
#[cfg(test)]
mod tests {
    extern "C" {
        fn mi_test_fifo() -> u32;
        fn mi_test_reroute() -> u32;
        fn mi_test_overflow() -> u32;
        fn mi_test_messages() -> u32;
        fn mi_test_panic() -> u32;
        fn mi_test_reset_presses() -> u32;
        fn mi_test_token_wrap() -> u32;
        fn mi_test_ring_wrap() -> u32;
        fn mi_test_dispatch() -> u32;
    }
    macro_rules! case {
        ($name:ident, $run:ident) => {
            #[test]
            fn $name() {
                assert_eq!(unsafe { $run() }, 0, "C assertion line");
            }
        };
    }
    case!(repeated_notes_pair_fifo_and_acks_are_idempotent, mi_test_fifo);
    case!(old_release_cannot_kill_rerouted_note, mi_test_reroute);
    case!(
        overflow_resets_and_quarantines_until_explicit_recovery,
        mi_test_overflow
    );
    case!(
        normalization_ports_channels_and_stock_fallthrough,
        mi_test_messages
    );
    case!(panic_retains_old_physical_release_pairing, mi_test_panic);
    case!(
        ignored_presses_survive_reset_and_source_changes,
        mi_test_reset_presses
    );
    case!(token_wrap_requires_new_epoch, mi_test_token_wrap);
    case!(ring_reuse_does_not_accept_stale_acks, mi_test_ring_wrap);
    case!(
        usb_and_din_share_ownership_without_consuming_upload_or_clock,
        mi_test_dispatch
    );
}
