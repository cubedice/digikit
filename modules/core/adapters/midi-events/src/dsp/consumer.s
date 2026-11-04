// Internal V2 lifecycle bridge, not a public descriptor ABI.
// Called only by the validated ordered receiver. Scratch contract matches it.
// Optional callbacks return exactly 1 after atomic application, else no effects.
// reset(R4=packet): new epoch must silence ALL instances; otherwise reset track.
// events(R4=packet): consume entire bounded batch or reject without effects.
// configure(R4=values,R8=metadata): validate schema and copy values, never retain.
// A callback must preserve I3+, M0..4/M7+, L3+ and all other non-scratch state.
.SECTION/PM seg_pmco;
.GLOBAL _mi_apply_packet;
.GLOBAL _mi_config_publish;
.EXTERN _mi_epoch;
.EXTERN _mi_config_apply;
.EXTERN _mi_config_abort;
.EXTERN _instrument_reset;
.EXTERN _instrument_events;
.EXTERN _instrument_configure;
.EXTERN _mi_instances;
_mi_apply_packet:
    DM(_mc_packet) = R4;
    I0 = R4;
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF EQ JUMP _mc_reset_instance;
    R1 = FEXT R0 BY 0:8;
    R2 = 1;
    COMP(R1, R2);
    IF EQ JUMP _mc_events;
    R2 = 5;
    COMP(R1, R2);
    IF EQ JUMP _mc_events;
    R5 = DM(8, I0); // config track/generation/revision
    R6 = DM(9, I0);
    R7 = DM(10, I0);
    CALL _mc_instance;
    R0 = DM(0, I1);
    COMP(R0, R6);
    IF NE JUMP _mc_no;
    R0 = DM(1, I1);
    R1 = 0x80000000;
    R0 = R0 XOR R1;
    R7 = R7 XOR R1;
    COMP(R7, R0);
    IF LE JUMP _mc_no;
    R4 = DM(_mc_packet);
    CALL _mi_config_apply;
    RTS;
_mc_events:
    R0 = DM(10, I0);
    R5 = FEXT R0 BY 10:4;
    R6 = DM(9, I0);
    R7 = DM(5, I0);
    CALL _mc_instance;
    R0 = DM(0, I1);
    COMP(R0, R6);
    IF NE JUMP _mc_no;
    R0 = DM(1, I1);
    COMP(R0, R7);
    IF NE JUMP _mc_no;
    R0 = _instrument_events;
    R0 = PASS R0;
    IF EQ JUMP _mc_no;
    R4 = DM(_mc_packet);
    CALL _instrument_events;
    RTS;
_mc_reset_instance:
    R5 = DM(8, I0);
    R6 = DM(9, I0);
    CALL _mc_instance;
    R0 = DM(2, I0);
    R1 = DM(_mi_epoch);
    COMP(R0, R1);
    IF NE JUMP _mc_reset_ready; // receiver has established a newer epoch
    R0 = DM(0, I1);
    R1 = 0x80000000;
    R0 = R0 XOR R1;
    R6 = R6 XOR R1;
    COMP(R6, R0);
    IF LE JUMP _mc_no; // no same-generation reset replay under a new message ID
_mc_reset_ready:
    R0 = _instrument_reset;
    R0 = PASS R0;
    IF EQ JUMP _mc_no;
    R4 = DM(_mc_packet);
    CALL _instrument_reset;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _mc_no;
    R4 = DM(_mc_packet);
    I0 = R4;
    R0 = DM(2, I0);
    R1 = DM(_mi_epoch);
    COMP(R0, R1);
    IF EQ JUMP _mc_reset_track;
    I1 = _mi_instances;
    R0 = 0;
    R1 = 32;
_mc_clear_instances:
    DM(I1, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _mc_clear_instances;
_mc_reset_track:
    R5 = DM(8, I0);
    R6 = DM(9, I0);
    CALL _mc_instance;
    DM(0, I1) = R6;
    R0 = 0;
    DM(1, I1) = R0;
    CALL _mi_config_abort;
    R0 = 1;
    RTS;
// Shared bank lookup: R5 track -> I1 pair {generation, applied revision}.
_mc_instance:
    R0 = LSHIFT R5 BY 3;
    R1 = _mi_instances;
    R0 = R0 + R1;
    I1 = R0;
    RTS;
_mi_config_publish:
    DM(_mc_metadata) = R8;
    // Metadata already validated against current ownership by apply_packet.
    R0 = _instrument_configure;
    R0 = PASS R0;
    IF EQ JUMP _mc_no;
    CALL _instrument_configure;
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _mc_no;
    R8 = DM(_mc_metadata);
    I0 = R8;
    R5 = DM(0, I0);
    R7 = DM(2, I0);
    CALL _mc_instance;
    DM(1, I1) = R7;
    R0 = 1;
    RTS;
_mc_no:
    R0 = 0;
    RTS;
.SECTION/DM seg_dmda;
.VAR _mc_packet = 0;
.VAR _mc_metadata = 0;
.PKBSS _mi_instances, 128;
