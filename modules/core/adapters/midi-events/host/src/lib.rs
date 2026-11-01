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
        fn mi_test_transport_batch() -> u32;
        fn mi_test_transport_reset() -> u32;
        fn mi_test_transport_partition() -> u32;
        fn mi_test_transport_timeout() -> u32;
        fn mi_test_transport_wrap() -> u32;
        fn mi_test_transport_wire() -> u32;
        fn mi_test_config_transaction() -> u32;
        fn mi_test_config_maximum() -> u32;
        fn mi_test_reply_ack() -> u32;
        fn mi_test_reply_watchdog() -> u32;
        fn mi_test_session_guard() -> u32;
        fn mi_test_reply_reordering() -> u32;
        fn mi_test_play_reply() -> u32;
        fn mi_test_session_keys() -> u32;
        fn mi_test_session_midi() -> u32;
        fn mi_test_reply_record() -> u32;
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
    case!(reply_requires_valid_fresh_identity, mi_test_reply_ack);
    case!(
        reply_watchdog_covers_idle_and_counter_wrap,
        mi_test_reply_watchdog
    );
    case!(
        session_guard_requires_one_shot_identity,
        mi_test_session_guard
    );
    case!(reply_uses_only_reserved_record_slots, mi_test_reply_record);
    case!(bounded_old_replies_never_ack_or_extend_trust, mi_test_reply_reordering);
    case!(versioned_playback_status_is_validated, mi_test_play_reply);
    case!(session_bound_midi_ack_and_revocation, mi_test_session_midi);
    case!(session_transition_preserves_old_key_release, mi_test_session_keys);
    case!(
        configuration_commits_only_after_final_ack,
        mi_test_config_transaction
    );
    case!(configuration_bounds_wrap_and_timeout, mi_test_config_maximum);
    case!(
        transport_retains_batches_until_exact_ack,
        mi_test_transport_batch
    );
    case!(transport_reset_supersedes_old_notes, mi_test_transport_reset);
    case!(
        transport_groups_track_and_configuration,
        mi_test_transport_partition
    );
    case!(transport_timeout_disables_routes, mi_test_transport_timeout);
    case!(transport_never_reuses_message_identity, mi_test_transport_wrap);
    case!(transport_emits_cross_target_wire_vectors, mi_test_transport_wire);
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
