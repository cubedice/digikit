// Stage-1 integration only: notes and fixed sine settings. Not installed by a
// manifest, and no CF readiness provider exists. No public V2 descriptor yet.
// Entry contracts: core/midi-events consumer.s. Never mutate a bank until the
// ENTIRE event batch has succeeded in scratch. No reentrant calls or rendering.
// One bank per track; pending reset tails finish before those slots can be reused.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_reset;
.GLOBAL _instrument_configure;
.GLOBAL _instrument_events;
.GLOBAL _cp_render_track;
.EXTERN _mi_epoch;
.EXTERN _cp_note_on;
.EXTERN _cp_note_off;
.EXTERN _cp_render;
.EXTERN _cp_banks;
.EXTERN _cp_ready;
.EXTERN _cp_staging;
_instrument_reset:
    I0 = R4;
    R0 = DM(2, I0);
    R1 = DM(_mi_epoch);
    COMP(R0, R1);
    IF NE JUMP _ci_reset_all;
    R5 = DM(8, I0);
    R0 = LSHIFT R5 BY 7;
    R1 = _cp_banks;
    R0 = R0 + R1;
    I0 = R0;
    R0 = LSHIFT R5 BY 2;
    R1 = _cp_ready;
    R0 = R0 + R1;
    I1 = R0;
    R0 = 0;
    DM(0, I1) = R0;
    R2 = 4;
    JUMP _ci_reset_slots;
_ci_reset_all:
    I0 = _cp_banks;
    I1 = _cp_ready;
    R2 = 16;
    R0 = 0;
_ci_clear_ready:
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_clear_ready;
    R2 = 64;
_ci_reset_slots:
    R0 = 2;
    DM(1, I0) = R0;
    MODIFY(I0, 8) (NW);
    R2 = R2 - 1;
    IF NE JUMP _ci_reset_slots;
    R0 = 1;
    RTS;
_instrument_configure:
    // Internal schema CPS1: one word, value 1 = fixed sine PLAY proof.
    I0 = R8;
    R0 = DM(4, I0);
    R1 = 4;
    COMP(R0, R1);
    IF NE JUMP _ci_reject;
    R0 = DM(5, I0);
    R1 = 0x43505331;
    COMP(R0, R1);
    IF NE JUMP _ci_reject;
    I1 = R4;
    R0 = DM(0, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _ci_reject;
    R0 = DM(0, I0);
    R0 = LSHIFT R0 BY 2;
    R1 = _cp_ready;
    R0 = R0 + R1;
    I1 = R0;
    R0 = 1;
    DM(0, I1) = R0;
    RTS;
_instrument_events:
    I0 = R4;
    R0 = DM(10, I0);
    R5 = FEXT R0 BY 10:4;
    R0 = LSHIFT R5 BY 2;
    R1 = _cp_ready;
    R0 = R0 + R1;
    I1 = R0;
    R0 = DM(0, I1);
    R0 = PASS R0;
    IF EQ JUMP _ci_reject;
    R0 = LSHIFT R5 BY 7;
    R1 = _cp_banks;
    R0 = R0 + R1;
    DM(_ci_bank) = R0;
    I1 = R0;
    I2 = _cp_staging;
    R2 = 32;
_ci_snapshot:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_snapshot;
    R0 = DM(1, I0);
    R0 = FEXT R0 BY 8:4;
    DM(_ci_remaining) = R0;
    MODIFY(I0, 8) (NW);
    R0 = I0;
    DM(_ci_event) = R0;
_ci_event_loop:
    R0 = DM(_ci_event);
    I0 = R0;
    R8 = DM(0, I0);
    R0 = DM(2, I0);
    R1 = FEXT R0 BY 0:4;
    R12 = FEXT R0 BY 14:7;
    R9 = FEXT R0 BY 21:7;
    R4 = _cp_staging;
    R2 = 1;
    COMP(R1, R2);
    IF EQ JUMP _ci_on;
    R2 = 2;
    COMP(R1, R2);
    IF NE JUMP _ci_reject; // unsupported controller: do not ACK or partly apply
    CALL _cp_note_off;
    // Unknown release is harmless: it may belong to a voice already quiet.
    JUMP _ci_next_event;
_ci_on:
    CALL _cp_note_on;
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
    R2 = 32;
_ci_commit:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _ci_commit;
    R0 = 1;
    RTS;
_ci_reject:
    R0 = 0;
    RTS;
_cp_render_track:
    // Internal render entry: R4=track (0..15), R8=output. Tails render even
    // after configuration was reset. Caller provides a valid track, no ISR race.
    R0 = LSHIFT R4 BY 7;
    R1 = _cp_banks;
    R4 = R0 + R1;
    CALL _cp_render;
    RTS;
.SECTION/DM seg_dmda;
.VAR _ci_bank = 0;
.VAR _ci_event = 0;
.VAR _ci_remaining = 0;
.PKBSS _cp_banks, 2048;
.PKBSS _cp_ready, 64;
.PKBSS _cp_staging, 128;
