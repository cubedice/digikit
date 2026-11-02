// Atomic event engine shared by the legacy proof and admitted V2 CANOPY.
.SECTION/PM seg_pmco;
.EXTERN _cp_note_on;
.EXTERN _cp_note_off;
.EXTERN _cp_retune;
// Internal shared atomic event engine. Explicit instance storage, independent
// of the legacy track-bank lookup: R4 bank, R8 pedal state, R12 triples, R9 count.
.GLOBAL _cp_apply_packed;
_cp_apply_packed:
    R0 = PASS R9;
    IF LE JUMP _ci_reject;
    R0 = 8;
    COMP(R9, R0);
    IF GT JUMP _ci_reject;
    DM(_ci_remaining) = R9;
    DM(_ci_event) = R12;
    DM(_ci_bank) = R4;
    DM(_ci_pedal_bank) = R8;
    I1 = R8;
    I2 = _ci_pedal_staging;
    R2 = 64;
_ci_snapshot_pedals:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_snapshot_pedals;
    R0 = DM(_ci_bank);
    I1 = R0;
    I2 = _cp_staging;
    R2 = 64;
_ci_snapshot:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_snapshot;
_ci_event_loop:
    R0 = DM(_ci_event);
    I0 = R0;
    R8 = DM(0, I0);
    R0 = DM(2, I0);
    R2 = FEXT R0 BY 4:2; // route = port * 16 + channel
    R2 = LSHIFT R2 BY 4;
    R3 = FEXT R0 BY 6:4;
    R2 = R2 + R3;
    DM(_ci_route_index) = R2;
    R3 = 1;
    R3 = LSHIFT R3 BY R2;
    DM(_ci_route_mask) = R3;
    R1 = FEXT R0 BY 0:4;
    R12 = FEXT R0 BY 14:7;
    R9 = FEXT R0 BY 21:7;
    R4 = _cp_staging;
    R2 = 1;
    COMP(R1, R2);
    IF EQ JUMP _ci_on;
    R2 = 2;
    COMP(R1, R2);
    IF NE JUMP _ci_controller;
    R0 = DM(_ci_pedal_staging);
    R1 = DM(_ci_route_mask);
    R0 = R0 AND R1;
    IF EQ JUMP _ci_release_now;
    CALL _ci_defer_release;
    JUMP _ci_next_event;
_ci_release_now:
    CALL _cp_note_off;
    // Unknown release is harmless: it may belong to a voice already quiet.
    JUMP _ci_next_event;
_ci_on:
    CALL _cp_note_on;
    R0 = PASS R0;
    IF EQ JUMP _ci_reject;
    CALL _ci_track_owner;
    R0 = PASS R0;
    IF EQ JUMP _ci_reject;
_ci_next_event:
    R0 = DM(_ci_event);
    R1 = 12;
    R0 = R0 + R1;
    DM(_ci_event) = R0;
    R0 = DM(_ci_remaining);
    R0 = R0 - 1;
    DM(_ci_remaining) = R0;
    IF NE JUMP _ci_event_loop;
    R0 = DM(_ci_bank);
    I1 = R0;
    I2 = _cp_staging;
    R2 = 64;
_ci_commit:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_commit;
    R0 = DM(_ci_pedal_bank);
    I1 = R0;
    I2 = _ci_pedal_staging;
    R2 = 64;
_ci_commit_pedals:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_commit_pedals;
    R0 = 1;
    RTS;
_ci_reject:
    R0 = 0;
    RTS;
// Sustain is scoped to port/channel. Only logical foreground owners can be
// deferred: a pending replacement supersedes the fading foreground token.
// Four records suffice for four admitted owners; stale stolen tokens cannot
// release replacements because note_off always matches the exact token.
_ci_defer_release:
    I0 = _cp_staging;
    R0 = _cp_staging;
    R1 = 192;
    R0 = R0 + R1;
    I1 = R0;
    I2 = _ci_pedal_staging;
    MODIFY(I2, 1) (NW);
    R2 = 4;
_ci_find_deferred_slot:
    R0 = DM(0, I0);
    R1 = DM(0, I1);
    R1 = PASS R1;
    IF NE R0 = R1;
    COMP(R0, R8);
    IF EQ JUMP _ci_store_deferred;
    MODIFY(I0, 8) (NW);
    MODIFY(I1, 4) (NW);
    MODIFY(I2, 2) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_find_deferred_slot;
    RTS;
_ci_store_deferred:
    DM(0, I2) = R8;
    R0 = DM(_ci_route_mask);
    DM(1, I2) = R0;
    RTS;
_ci_controller:
    R2 = 4;
    COMP(R1, R2);
    IF EQ JUMP _ci_bend_event;
    R2 = 3;
    COMP(R1, R2);
    IF NE JUMP _ci_reject;
    R2 = 120;
    COMP(R12, R2);
    IF EQ JUMP _ci_all_sound_off;
    R2 = 123;
    COMP(R12, R2);
    IF EQ JUMP _ci_all_notes_off;
    R2 = 121; // Reset All Controllers releases pedal ownership on this route.
    COMP(R12, R2);
    IF EQ JUMP _ci_reset_controllers;
    R2 = 64;
    COMP(R12, R2);
    IF NE JUMP _ci_reject;
    COMP(R9, R2);
    IF LT JUMP _ci_pedal_up;
    R0 = DM(_ci_pedal_staging);
    R1 = DM(_ci_route_mask);
    R0 = R0 OR R1;
    DM(_ci_pedal_staging) = R0;
    JUMP _ci_next_event;
_ci_pedal_up:
    R0 = DM(_ci_pedal_staging);
    R1 = DM(_ci_route_mask);
    R1 = NOT R1;
    R0 = R0 AND R1;
    DM(_ci_pedal_staging) = R0;
    R0 = 0;
    DM(_ci_release_index) = R0;
_ci_release_deferred:
    R0 = DM(_ci_release_index);
    R0 = LSHIFT R0 BY 3;
    R1 = _ci_pedal_staging;
    R0 = R0 + R1;
    I0 = R0;
    R8 = DM(1, I0);
    R8 = PASS R8;
    IF EQ JUMP _ci_next_deferred;
    R0 = DM(2, I0);
    R1 = DM(_ci_route_mask);
    COMP(R0, R1);
    IF NE JUMP _ci_next_deferred;
    R0 = 0;
    DM(1, I0) = R0;
    DM(2, I0) = R0;
    R4 = _cp_staging;
    CALL _cp_note_off;
_ci_next_deferred:
    R0 = DM(_ci_release_index);
    R0 = R0 + 1;
    DM(_ci_release_index) = R0;
    R1 = 4;
    COMP(R0, R1);
    IF LT JUMP _ci_release_deferred;
    JUMP _ci_next_event;
// Ten token/route entries cover six sounding slots plus four pending onsets.
// Prune only after a successful onset, then remember its route. State is staged.
_ci_owner_at:
    R0 = DM(_ci_owner_index);
    R0 = LSHIFT R0 BY 3;
    R1 = _ci_pedal_staging;
    R0 = R0 + R1;
    I2 = R0;
    MODIFY(I2, 9) (NW);
    RTS;
_ci_token_alive:
    I0 = _cp_staging;
    R2 = 6;
_ci_alive_voice:
    R0 = DM(1, I0);
    R1 = 4;
    R0 = R0 AND R1;
    IF NE JUMP _ci_alive_next;
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _ci_alive;
_ci_alive_next:
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_alive_voice;
    R2 = 4;
_ci_alive_pending:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF EQ JUMP _ci_alive;
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_alive_pending;
    R0 = 0;
    RTS;
_ci_alive:
    R0 = 1;
    RTS;
_ci_track_owner:
    R0 = 0;
    DM(_ci_owner_index) = R0;
_ci_prune_owner:
    CALL _ci_owner_at;
    R8 = DM(0, I2);
    R8 = PASS R8;
    IF EQ JUMP _ci_prune_next;
    CALL _ci_token_alive;
    R0 = PASS R0;
    IF NE JUMP _ci_prune_next;
    DM(0, I2) = R0;
    DM(1, I2) = R0;
_ci_prune_next:
    R0 = DM(_ci_owner_index);
    R0 = R0 + 1;
    DM(_ci_owner_index) = R0;
    R1 = 10;
    COMP(R0, R1);
    IF LT JUMP _ci_prune_owner;
    R0 = 0;
    DM(_ci_owner_index) = R0;
_ci_insert_owner:
    CALL _ci_owner_at;
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ JUMP _ci_insert_here;
    R0 = DM(_ci_owner_index);
    R0 = R0 + 1;
    DM(_ci_owner_index) = R0;
    R1 = 10;
    COMP(R0, R1);
    IF LT JUMP _ci_insert_owner;
    R0 = 0; // Fail the batch closed if the bank/table invariant is violated.
    RTS;
_ci_insert_here:
    R0 = DM(_ci_event);
    I0 = R0;
    R0 = DM(0, I0);
    DM(0, I2) = R0;
    R0 = DM(2, I0);
    R0 = FEXT R0 BY 14:7;
    R0 = LSHIFT R0 BY 8;
    R1 = DM(_ci_route_index);
    R0 = R0 OR R1;
    DM(1, I2) = R0;
    CALL _ci_retune_owner;
    R0 = 1;
    RTS;
_ci_all_sound_off:
    R0 = 1;
    DM(_ci_panic) = R0;
    R0 = DM(_ci_pedal_staging);
    R1 = DM(_ci_route_mask);
    R1 = NOT R1;
    R0 = R0 AND R1;
    DM(_ci_pedal_staging) = R0;
    // Clear route-owned deferred records without touching other routes.
    I0 = _ci_pedal_staging;
    MODIFY(I0, 1) (NW);
    R2 = 4;
_ci_panic_deferred:
    R0 = DM(1, I0);
    R1 = DM(_ci_route_mask);
    COMP(R0, R1);
    IF NE JUMP _ci_panic_deferred_next;
    R0 = 0;
    DM(0, I0) = R0;
    DM(1, I0) = R0;
_ci_panic_deferred_next:
    MODIFY(I0, 2) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_panic_deferred;
    JUMP _ci_route_release;
_ci_all_notes_off:
    R0 = 0;
    DM(_ci_panic) = R0;
_ci_route_release:
    R0 = 0;
    DM(_ci_owner_index) = R0;
_ci_route_release_next:
    CALL _ci_owner_at;
    R8 = DM(0, I2);
    R8 = PASS R8;
    IF EQ JUMP _ci_route_advance;
    R0 = DM(1, I2);
    R0 = FEXT R0 BY 0:5;
    R1 = DM(_ci_route_index);
    COMP(R0, R1);
    IF NE JUMP _ci_route_advance;
    R0 = DM(_ci_panic);
    R0 = PASS R0;
    IF NE JUMP _ci_route_panic;
    R0 = DM(_ci_pedal_staging);
    R1 = DM(_ci_route_mask);
    R0 = R0 AND R1;
    IF EQ JUMP _ci_route_normal;
    CALL _ci_defer_release;
    JUMP _ci_route_advance;
_ci_route_normal:
    R4 = _cp_staging;
    CALL _cp_note_off;
    JUMP _ci_route_advance;
_ci_route_panic:
    // Cancel pending onsets and fast-ramp every matching sounding slot.
    I0 = _cp_staging;
    R2 = 6;
_ci_panic_voice:
    R0 = DM(1, I0);
    R1 = 4;
    R0 = R0 AND R1;
    IF NE JUMP _ci_panic_voice_next;
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF NE JUMP _ci_panic_voice_next;
    R0 = 2;
    DM(1, I0) = R0;
_ci_panic_voice_next:
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_panic_voice;
    R2 = 4;
_ci_panic_pending:
    R0 = DM(0, I0);
    COMP(R0, R8);
    IF NE JUMP _ci_panic_pending_next;
    R0 = 0;
    DM(0, I0) = R0;
_ci_panic_pending_next:
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_panic_pending;
_ci_route_advance:
    R0 = DM(_ci_owner_index);
    R0 = R0 + 1;
    DM(_ci_owner_index) = R0;
    R1 = 10;
    COMP(R0, R1);
    IF LT JUMP _ci_route_release_next;
    JUMP _ci_next_event;
// Signed per-route bend values live at word 29..60 of controller state.
_ci_bend_address:
    R0 = DM(_ci_route_index);
    R0 = LSHIFT R0 BY 2;
    R1 = _ci_pedal_staging;
    R0 = R0 + R1;
    I0 = R0;
    MODIFY(I0, 29) (NW);
    RTS;
_ci_bend_event:
    R9 = LSHIFT R9 BY 7;
    R9 = R9 + R12;
    R0 = 8192;
    R9 = R9 - R0;
    CALL _ci_bend_address;
    DM(0, I0) = R9;
    CALL _ci_apply_route_bend;
    JUMP _ci_next_event;
_ci_reset_controllers:
    CALL _ci_bend_address;
    R0 = 0;
    DM(0, I0) = R0;
    CALL _ci_apply_route_bend;
    JUMP _ci_pedal_up;
_ci_apply_route_bend:
    R0 = 0;
    DM(_ci_owner_index) = R0;
_ci_bend_next_owner:
    CALL _ci_owner_at;
    R0 = DM(0, I2);
    R0 = PASS R0;
    IF EQ JUMP _ci_bend_advance;
    R0 = DM(1, I2);
    R0 = FEXT R0 BY 0:5;
    R1 = DM(_ci_route_index);
    COMP(R0, R1);
    IF NE JUMP _ci_bend_advance;
    CALL _ci_retune_owner;
_ci_bend_advance:
    R0 = DM(_ci_owner_index);
    R0 = R0 + 1;
    DM(_ci_owner_index) = R0;
    R1 = 10;
    COMP(R0, R1);
    IF LT JUMP _ci_bend_next_owner;
    RTS;
_ci_retune_owner:
    R0 = DM(0, I2);
    DM(_ci_bend_token) = R0;
    R0 = DM(1, I2);
    R1 = FEXT R0 BY 8:7;
    DM(_ci_bend_note) = R1;
    R0 = FEXT R0 BY 0:5;
    R0 = LSHIFT R0 BY 2;
    R1 = _ci_pedal_staging;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(29, I1);
    DM(_ci_bend_value) = R0;
    R0 = 0;
    DM(_ci_bend_slot) = R0;
_ci_bend_voice:
    R0 = DM(_ci_bend_slot);
    R0 = LSHIFT R0 BY 5;
    R1 = _cp_staging;
    R0 = R0 + R1;
    I0 = R0;
    R0 = DM(1, I0);
    R1 = 4;
    R0 = R0 AND R1;
    IF NE JUMP _ci_bend_voice_next;
    R0 = DM(0, I0);
    R1 = DM(_ci_bend_token);
    COMP(R0, R1);
    IF NE JUMP _ci_bend_voice_next;
    R12 = DM(_ci_bend_note);
    R9 = DM(_ci_bend_value);
    CALL _cp_retune;
_ci_bend_voice_next:
    R0 = DM(_ci_bend_slot);
    R0 = R0 + 1;
    DM(_ci_bend_slot) = R0;
    R1 = 6;
    COMP(R0, R1);
    IF LT JUMP _ci_bend_voice;
    I0 = _cp_staging;
    MODIFY(I0, 48) (NW);
    R2 = 4;
_ci_bend_pending:
    R0 = DM(0, I0);
    R1 = DM(_ci_bend_token);
    COMP(R0, R1);
    IF NE JUMP _ci_bend_pending_next;
    R0 = DM(_ci_bend_value);
    DM(3, I0) = R0;
_ci_bend_pending_next:
    MODIFY(I0, 4) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_bend_pending;
    RTS;
.SECTION/DM seg_dmda;
.VAR _ci_bank = 0;
.VAR _ci_event = 0;
.VAR _ci_remaining = 0;
// These persist across events/resets, so the boot stream must initialize them.
// PKBSS only reserves DSP memory; it does not establish empty tokens/readiness.


.PKBSS _cp_staging, 256;

// Per track: 32 route pedal bits, four {deferred token, route mask} records,
// ten {live token, route index | note<<8} pairs, 32 signed bends, three reserved.

.PKBSS _ci_pedal_staging, 256;
.VAR _ci_pedal_bank = 0;
.VAR _ci_route_mask = 0;
.VAR _ci_release_index = 0;

.VAR _ci_owner_index = 0;
.VAR _ci_panic = 0;

.VAR _ci_route_index = 0;
.VAR _ci_bend_token = 0;
.VAR _ci_bend_note = 0;
.VAR _ci_bend_value = 0;
.VAR _ci_bend_slot = 0;
