// Stage 1: one admitted row, one instance, track 0. Transport callbacks run
// under the saved early-frame scalar context. No module callback is a V1 voice.
// Events are copied once, consumed once by process, then removed from the ABI.
.SECTION/PM seg_pmco;
.GLOBAL _instrument_reset;
.GLOBAL _instrument_configure;
.GLOBAL _instrument_events;
.GLOBAL _instrument_carrier_prepare;
.GLOBAL _iv_voice_render;
.GLOBAL _iv_reset_row;
.EXTERN _ir_rows;
.EXTERN _ir_process_rows;
.EXTERN _ir_process_protected;
.EXTERN _ir_normalize_packet;
.EXTERN _ir_reset_instance;
.EXTERN _iv_reset_protected;
.EXTERN _mi_epoch;
.EXTERN _instrument_session;
.EXTERN _ir_lifecycle_ready;
.EXTERN _mi_velocity_prepare;
.EXTERN _mi_carrier_rendered;

// This adapter is absent from ordinary images. Linking it is the trusted
// single-instance capability; binding the session must precede lifecycle work.
_instrument_carrier_prepare:
    I0 = _ir_rows;
    R0 = DM(0, I0);
    DM(_instrument_play_status) = R0;
    R0 = DM(2, I0);
    DM(_instrument_play_status + 4) = R0;
    R0 = DM(_instrument_session);
    R0 = PASS R0;
    IF EQ RTS;
    R0 = 1;
    DM(_ir_lifecycle_ready) = R0;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet;
    I0 = _ir_process_rows;
    R0 = DM(2, I0);
    I1 = _instrument_voice_rows;
    R1 = _iv_voice_render;
    DM(1, I1) = R1;
    DM(0, I1) = R0; // publish the admitted type last; never shadow a stock type
    R0 = DM(_iv_generation);
    R0 = PASS R0;
    IF EQ JUMP _iv_quiet;
    I0 = _iv_input;
    R0 = 2;
    DM(0, I0) = R0;
    R0 = 72;
    DM(1, I0) = R0;
    R0 = DM(_iv_generation);
    DM(3, I0) = R0;
    R0 = _iv_output;
    DM(5, I0) = R0;
    R0 = _iv_events;
    DM(7, I0) = R0;
    R0 = _iv_controls;
    DM(9, I0) = R0;
    R0 = 1;
    DM(10, I0) = R0;
    // A reset tail renders before configuration. No new events are admitted.
    R1 = DM(_iv_revision);
    R1 = PASS R1;
    IF EQ R1 = R0;
    DM(11, I0) = R1;
    R4 = 0;
    R5 = 0;
    R8 = _iv_input;
    CALL _ir_process_protected;
    DM(_iv_result) = R0;
    I0 = _iv_input;
    R1 = 0;
    DM(8, I0) = R1;
    R0 = PASS R0;
    IF LT JUMP _iv_quiet;
    R1 = 1;
    R0 = R1 - R0; // ACTIVE=0, QUIET=1
    DM(_iv_active) = R0;
    R10 = DM(_iv_revision);
    R10 = R10 OR R0;
    R4 = 0;
    CALL _mi_velocity_prepare;
    RTS;
_iv_quiet:
    R0 = 0;
    DM(_iv_active) = R0;
    DM(_iv_revision) = R0;
    I0 = _iv_input;
    DM(8, I0) = R0;
    I0 = _iv_output;
    R1 = 32;
_iv_clear_output:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_clear_output;
    RTS;

// A receiver reset can precede CREATE. Such a reset establishes only transport
// identity; no callback or state pointer is used until the admitted row is ready.
_instrument_reset:
    DM(_iv_packet) = R4;
    I0 = R4;
    R0 = DM(8, I0);
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_owned;
    R0 = DM(2, I0);
    R1 = DM(_mi_epoch);
    COMP(R0, R1);
    IF NE JUMP _iv_reset_owned;
    R0 = 1;
    RTS; // inactive tracks still acknowledge the transport's all-track reset
_iv_reset_owned:
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_identity;
    CALL _iv_reset_protected;
    R0 = PASS R0;
    IF NE JUMP _iv_no;
_iv_reset_identity:
    R0 = DM(_iv_packet);
    I0 = R0;
    R0 = DM(8, I0);
    R0 = PASS R0;
    IF NE JUMP _iv_reset_unconfigured;
    R0 = DM(9, I0);
    DM(_iv_generation) = R0;
_iv_reset_unconfigured:
    R0 = 0;
    DM(_iv_revision) = R0;
    I0 = _iv_input;
    DM(8, I0) = R0;
    R0 = 1;
    RTS;

_instrument_configure:
    DM(_iv_values) = R4;
    DM(_iv_metadata) = R8;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    // A replacement's bounded release tail must finish before new ownership.
    R0 = DM(_iv_active);
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    R0 = DM(_iv_metadata);
    I0 = R0;
    R0 = DM(0, I0);
    R0 = PASS R0;
    IF NE JUMP _iv_no;
    R0 = DM(1, I0);
    R1 = DM(_iv_generation);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(4, I0);
    R1 = 4;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(5, I0);
    R1 = 0x43505331;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(_iv_values);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(2, I0);
    DM(_iv_revision) = R0;
    R0 = 1;
    RTS;

_instrument_events:
    DM(_iv_packet) = R4;
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    R0 = DM(_iv_revision);
    R0 = PASS R0;
    IF EQ JUMP _iv_no;
    I0 = _iv_input;
    R0 = DM(8, I0);
    R0 = PASS R0;
    IF NE JUMP _iv_no; // never overwrite an unconsumed batch
    R4 = DM(_iv_packet);
    R8 = _iv_input;
    R12 = DM(_mi_epoch);
    CALL _ir_normalize_packet;
    R0 = PASS R0;
    IF LE JUMP _iv_no;
    R0 = 1;
    RTS;
_iv_no:
    R0 = 0;
    RTS;

_iv_admitted:
    I0 = _ir_rows;
    R0 = DM(0, I0);
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(3, I0);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    I1 = _ir_process_rows;
    R0 = DM(0, I1);
    R1 = DM(1, I0);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(1, I1);
    R1 = DM(2, I0);
    COMP(R0, R1);
    IF NE JUMP _iv_no;
    R0 = DM(2, I1);
    R1 = 16;
    COMP(R0, R1);
    IF LT JUMP _iv_no;
    R1 = 255;
    COMP(R0, R1);
    IF GT JUMP _iv_no;
    R0 = 1;
    RTS;

_iv_reset_row:
    CALL _iv_admitted;
    R0 = PASS R0;
    IF EQ JUMP _iv_reset_bad;
    I0 = _ir_rows;
    R4 = DM(4, I0);
    R5 = DM(5, I0);
    R6 = DM(6, I0);
    R7 = DM(7, I0);
    R12 = DM(8, I0);
    R9 = DM(9, I0);
    R10 = DM(10, I0);
    R8 = 2;
    CALL _ir_reset_instance;
    R0 = PASS R0;
    IF EQ RTS;
    I0 = _ir_rows;
    R1 = 5;
    DM(0, I0) = R1;
    DM(12, I0) = R0;
    RTS;
_iv_reset_bad:
    R0 = -1;
    RTS;

// Raw stock-carrier ABI: copy the already-rendered block, never advance DSP
// twice. The stock wrapper duplicates this for the odd stereo voice.
_iv_voice_render:
    R0 = PASS R2;
    IF NE JUMP _iv_voice_silent;
    I0 = _iv_output;
    I1 = R8;
    R1 = 32;
_iv_voice_copy:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_voice_copy;
    R4 = 0;
    R8 = DM(_iv_active);
    R10 = DM(_iv_revision);
    CALL _mi_carrier_rendered;
    R0 = 0;
    JUMP (M13, I13);
_iv_voice_silent:
    I0 = R8;
    R1 = 32;
    R0 = 0;
_iv_voice_zero:
    DM(I0, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _iv_voice_zero;
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.PKDATA _instrument_voice_rows, 192;
.PKDATA _iv_input, 72;
.PKDATA _iv_output, 128;
.PKDATA _iv_events, 192;
.VAR _iv_controls = 1;
.VAR _iv_generation = 0;
.VAR _iv_revision = 0;
.VAR _iv_active = 0;
.VAR _iv_result = 0;
.PKBSS _iv_packet, 4;
.PKBSS _iv_values, 4;
.PKBSS _iv_metadata, 4;

.PKDATA _instrument_play_status, 8;
