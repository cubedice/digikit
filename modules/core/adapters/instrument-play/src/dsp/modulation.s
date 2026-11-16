// Native MOD consumes the stock six SRC mirrors once per audio block. Base
// configuration and presets are immutable. The callback sees a scratch bank
// with bounded effective values and the ordinary unchanged revision/schema.
// R4=base controls, R8=count. R0=base or scratch. Clobbers R0..15/I0..2.
// The completed stock frame is the same stable copy used by V1 instruments.
// It is refreshed later in render_frame: MOD deliberately has one-block latency.
// Reset, lost admission and a foreign frame invalidate that copy; the first
// valid block stays neutral so the previous owner's offsets cannot leak in.
.SECTION/PM seg_pmco;
.GLOBAL _iv_modulate;
.EXTERN _iv_mod_frame;
.EXTERN _ir_process_rows;
.EXTERN _iv_track;
_iv_modulate:
    R0 = PASS R4;
    R1 = 32;
    COMP(R8, R1);
    IF NE JUMP _iv_mod_invalid;
    I0 = R4;
    R1 = DM(0, I0);
    R1 = FEXT R1 BY 0:8;
    R2 = 11;
    COMP(R1, R2);
    IF NE JUMP _iv_mod_invalid; // Older diagnostic schemas stay unchanged.
    // Frame halfword 74 + track (byte 0x94 + 2t) holds that track's type.
    R1 = DM(_iv_track);
    R2 = LSHIFT R1 BY -4; // unsigned: only tracks 0..15 have a frame row
    R2 = PASS R2;
    IF NE JUMP _iv_mod_invalid;
    R2 = 74;
    R1 = R1 + R2;
    R2 = LSHIFT R1 BY 1;
    R3 = 0xfffffffc;
    R2 = R2 AND R3;
    R3 = _iv_mod_frame;
    R2 = R2 + R3;
    I0 = R2;
    R2 = DM(0, I0);
    R3 = FEXT R1 BY 0:1;
    R3 = LSHIFT R3 BY 4;
    R3 = -R3;
    R2 = LSHIFT R2 BY R3;
    R1 = FEXT R2 BY 0:16;
    I1 = _ir_process_rows;
    R2 = DM(2, I1);
    COMP(R1, R2);
    IF NE JUMP _iv_mod_invalid; // Never borrow a replacement track's modulation.
    R1 = DM(_iv_mod_ready);
    R1 = PASS R1;
    IF NE JUMP _iv_mod_current;
    R1 = 1;
    DM(_iv_mod_ready) = R1;
    RTS;
_iv_mod_invalid:
    R1 = 0;
    DM(_iv_mod_ready) = R1;
    RTS;
_iv_mod_current:
    DM(_iv_mod_base) = R4;
    I0 = _iv_mod_mirrors;
    I1 = _iv_mod_delta;
    R10 = 6;
    R11 = 0;
    R12 = DM(_iv_track);
    R13 = 48; // stock frame parameters: 48 halfwords (0x60 bytes) per track
    R12 = R12 * R13 (SSI);
_iv_mod_read:
    R2 = DM(I0, M6);
    R2 = R2 + R12;
    R3 = FEXT R2 BY 0:1;
    R3 = LSHIFT R3 BY 4;
    R3 = -R3;
    R1 = 0xfffffffe;
    R2 = R2 AND R1;
    R2 = LSHIFT R2 BY 1;
    R1 = _iv_mod_frame;
    R2 = R2 + R1;
    I2 = R2;
    R2 = DM(0, I2);
    R2 = LSHIFT R2 BY R3;
    R2 = FEXT R2 BY 0:16;
    R1 = 0x7f00;
    COMP(R2, R1);
    R1 = 0x4000;
    IF GT R2 = PASS R1;
    R2 = R2 - R1;
    DM(I1, M6) = R2;
    R11 = R11 OR R2;
    R10 = R10 - 1;
    IF NE JUMP _iv_mod_read;
    R0 = DM(_iv_mod_base);
    R11 = PASS R11;
    IF EQ RTS;
    I0 = R0;
    I1 = _iv_mod_controls;
    R2 = 32;
_iv_mod_copy:
    R1 = DM(I0, M6);
    DM(I1, M6) = R1;
    R2 = R2 - 1;
    IF NE JUMP _iv_mod_copy;
    I0 = _iv_mod_controls;
    R0 = DM(1, I0);
    R1 = 2;
    R11 = _iv_mod_pad;
    COMP(R0, R1);
    IF EQ JUMP _iv_mod_wt;
    R1 = 3;
    COMP(R0, R1);
    IF NE JUMP _iv_mod_begin;
    R11 = _iv_mod_fm;
    JUMP _iv_mod_begin;
_iv_mod_wt:
    R11 = _iv_mod_wavetable;
_iv_mod_begin:
    R10 = 0;
_iv_mod_lane:
    R0 = LSHIFT R10 BY 2;
    R1 = _iv_mod_delta;
    R0 = R0 + R1;
    I0 = R0;
    R8 = DM(0, I0);
    R8 = PASS R8;
    IF EQ JUMP _iv_mod_next;
    I0 = R11;
    R4 = DM(0, I0); // packed control word
    R5 = DM(1, I0); // bit shift (0 or16)
    R6 = DM(2, I0); // minimum
    R7 = DM(3, I0); // maximum
    // Synchronized scan rate is a bounded discrete choice (1..7), not Hz.
    I0 = _iv_mod_controls;
    R0 = DM(1, I0);
    R1 = 2;
    COMP(R0, R1);
    IF NE JUMP _iv_mod_pad_constraint;
    COMP(R10, R1);
    IF NE JUMP _iv_mod_apply;
    R0 = DM(22, I0);
    R0 = LSHIFT R0 BY -16;
    R0 = PASS R0;
    R1 = 7;
    IF NE R7 = PASS R1;
    JUMP _iv_mod_apply;
_iv_mod_pad_constraint:
    R1 = 2;
    COMP(R0, R1);
    IF GE JUMP _iv_mod_apply;
    R1 = 5;
    COMP(R10, R1);
    IF NE JUMP _iv_mod_apply;
    R0 = DM(13, I0);
    R1 = 1;
    COMP(R0, R1);
    IF EQ JUMP _iv_mod_next; // MASTER GRID deliberately forbids swing.
_iv_mod_apply:
    R0 = LSHIFT R4 BY 2;
    R1 = _iv_mod_controls;
    R0 = R0 + R1;
    I1 = R0;
    R12 = DM(0, I1);
    R0 = -R5;
    R1 = LSHIFT R12 BY R0;
    R1 = FEXT R1 BY 0:16;
    R2 = R7 - R6;
    F2 = FLOAT R2;
    F8 = FLOAT R8;
    F3 = 0.00006103515625; // native bipolar carrier scale: 1/16384
    F8 = F8 * F3;
    F8 = F8 * F2;
    R8 = TRUNC F8;
    R1 = R1 + R8;
    COMP(R1, R6);
    IF LT R1 = PASS R6;
    COMP(R1, R7);
    IF GT R1 = PASS R7;
    R2 = 0xffff;
    R2 = LSHIFT R2 BY R5;
    R2 = NOT R2;
    R12 = R12 AND R2;
    R2 = LSHIFT R1 BY R5;
    R12 = R12 OR R2;
    DM(0, I1) = R12;
    // Legacy/FM depth controls also have scalar copies in the wire contract.
    I0 = R11;
    R2 = DM(4, I0);
    R2 = PASS R2;
    IF LT JUMP _iv_mod_next;
    R0 = 15;
    COMP(R2, R0);
    IF NE JUMP _iv_mod_duplicate;
    R0 = LSHIFT R1 BY 1;
    R1 = R1 + R0;
    R1 = LSHIFT R1 BY -1; // strum's transport-unit conversion
_iv_mod_duplicate:
    R2 = LSHIFT R2 BY 2;
    R0 = _iv_mod_controls;
    R2 = R2 + R0;
    I0 = R2;
    DM(0, I0) = R1;
_iv_mod_next:
    R0 = 20;
    R11 = R11 + R0;
    R10 = R10 + 1;
    R0 = 6;
    COMP(R10, R0);
    IF LT JUMP _iv_mod_lane;
    R0 = _iv_mod_controls;
    RTS;

// Restore only normalized base-control fields in a successful CPSB detail
// fragment. UI ACK/pending feedback must describe the accepted base revision,
// not constantly moving LFO samples. Voice identity and fast telemetry stay real.
// R4=base controls, R8=process input. No borrowed pointer survives this call.
.GLOBAL _iv_mod_status;
_iv_mod_status:
    I0 = R8;
    R0 = DM(9, I0);
    R1 = _iv_mod_controls;
    COMP(R0, R1);
    IF EQ JUMP _iv_mod_status_scratch;
    R1 = _iv_lock_controls;
    COMP(R0, R1);
    IF NE RTS;
_iv_mod_status_scratch:
    R0 = DM(0, I0);
    R1 = 5;
    COMP(R0, R1);
    IF LT RTS;
    R0 = DM(22, I0);
    I1 = R0;
    R0 = DM(0, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(2, I1);
    R0 = LSHIFT R0 BY -30;
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(6, I1);
    R1 = DM(11, I0);
    COMP(R0, R1);
    IF NE RTS;
    R0 = DM(5, I1);
    R1 = LSHIFT R0 BY -8;
    R2 = 0x50b;
    COMP(R1, R2);
    IF NE RTS;
    R0 = FEXT R0 BY 0:8;
    R1 = 10;
    COMP(R0, R1);
    IF GT RTS;
    R1 = 6;
    R5 = R0 * R1 (SSI);
    R6 = 6;
    MODIFY(I1, 7) (NW);
_iv_mod_status_word:
    R0 = 32;
    COMP(R5, R0);
    IF GE RTS;
    R0 = 16;
    COMP(R5, R0);
    IF GE JUMP _iv_mod_status_high;
    R0 = R5 + R0;
    R0 = LSHIFT R0 BY 2;
    R0 = R0 + R4;
    I0 = R0;
    R0 = DM(0, I0);
    R0 = FEXT R0 BY 0:16;
    JUMP _iv_mod_status_store;
_iv_mod_status_high:
    R0 = LSHIFT R5 BY 2;
    R0 = R0 + R4;
    I0 = R0;
    R0 = DM(0, I0);
    R0 = LSHIFT R0 BY -16;
_iv_mod_status_store:
    DM(I1, M6) = R0;
    R5 = R5 + 1;
    R6 = R6 - 1;
    IF NE JUMP _iv_mod_status_word;
    RTS;

.SECTION/DM seg_dmda;
.PKBSS _iv_mod_ready, 4;
.PKBSS _iv_mod_base, 4;
.PKBSS _iv_mod_delta, 24;
.PKBSS _iv_mod_controls, 128;
// Table rows: word,shift,min,max,duplicate (0xffffffff means none).
.VAR _iv_mod_mirrors = 110;
.VAR _iv_mod_mirrors_1 = 111;
.VAR _iv_mod_mirrors_2 = 112;
.VAR _iv_mod_mirrors_3 = 115;
.VAR _iv_mod_mirrors_4 = 116;
.VAR _iv_mod_mirrors_5 = 117;
.VAR _iv_mod_pad = 18;
.VAR _iv_mod_pad_1 = 0;
.VAR _iv_mod_pad_2 = 0;
.VAR _iv_mod_pad_3 = 127;
.VAR _iv_mod_pad_4 = 2;
.VAR _iv_mod_pad_5 = 19;
.VAR _iv_mod_pad_6 = 0;
.VAR _iv_mod_pad_7 = 0;
.VAR _iv_mod_pad_8 = 127;
.VAR _iv_mod_pad_9 = 3;
.VAR _iv_mod_pad_10 = 21;
.VAR _iv_mod_pad_11 = 0;
.VAR _iv_mod_pad_12 = 0;
.VAR _iv_mod_pad_13 = 127;
.VAR _iv_mod_pad_14 = 6;
.VAR _iv_mod_pad_15 = 20;
.VAR _iv_mod_pad_16 = 0;
.VAR _iv_mod_pad_17 = 0;
.VAR _iv_mod_pad_18 = 500;
.VAR _iv_mod_pad_19 = 15;
.VAR _iv_mod_pad_20 = 25;
.VAR _iv_mod_pad_21 = 0;
.VAR _iv_mod_pad_22 = 1;
.VAR _iv_mod_pad_23 = 100;
.VAR _iv_mod_pad_24 = 9;
.VAR _iv_mod_pad_25 = 26;
.VAR _iv_mod_pad_26 = 0;
.VAR _iv_mod_pad_27 = 0;
.VAR _iv_mod_pad_28 = 75;
.VAR _iv_mod_pad_29 = 12;
.VAR _iv_mod_wavetable = 17;
.VAR _iv_mod_wavetable_1 = 16;
.VAR _iv_mod_wavetable_2 = 0;
.VAR _iv_mod_wavetable_3 = 700;
.VAR _iv_mod_wavetable_4 = 0xffffffff;
.VAR _iv_mod_wavetable_5 = 18;
.VAR _iv_mod_wavetable_6 = 16;
.VAR _iv_mod_wavetable_7 = 0;
.VAR _iv_mod_wavetable_8 = 100;
.VAR _iv_mod_wavetable_9 = 0xffffffff;
.VAR _iv_mod_wavetable_10 = 19;
.VAR _iv_mod_wavetable_11 = 16;
.VAR _iv_mod_wavetable_12 = 1;
.VAR _iv_mod_wavetable_13 = 1000;
.VAR _iv_mod_wavetable_14 = 0xffffffff;
.VAR _iv_mod_wavetable_15 = 23;
.VAR _iv_mod_wavetable_16 = 16;
.VAR _iv_mod_wavetable_17 = 0;
.VAR _iv_mod_wavetable_18 = 200;
.VAR _iv_mod_wavetable_19 = 0xffffffff;
.VAR _iv_mod_wavetable_20 = 28;
.VAR _iv_mod_wavetable_21 = 16;
.VAR _iv_mod_wavetable_22 = 0;
.VAR _iv_mod_wavetable_23 = 100;
.VAR _iv_mod_wavetable_24 = 0xffffffff;
.VAR _iv_mod_wavetable_25 = 30;
.VAR _iv_mod_wavetable_26 = 16;
.VAR _iv_mod_wavetable_27 = 0;
.VAR _iv_mod_wavetable_28 = 200;
.VAR _iv_mod_wavetable_29 = 0xffffffff;
.VAR _iv_mod_fm = 18;
.VAR _iv_mod_fm_1 = 0;
.VAR _iv_mod_fm_2 = 0;
.VAR _iv_mod_fm_3 = 127;
.VAR _iv_mod_fm_4 = 2;
.VAR _iv_mod_fm_5 = 17;
.VAR _iv_mod_fm_6 = 16;
.VAR _iv_mod_fm_7 = 0;
.VAR _iv_mod_fm_8 = 100;
.VAR _iv_mod_fm_9 = 0xffffffff;
.VAR _iv_mod_fm_10 = 20;
.VAR _iv_mod_fm_11 = 16;
.VAR _iv_mod_fm_12 = 0;
.VAR _iv_mod_fm_13 = 100;
.VAR _iv_mod_fm_14 = 0xffffffff;
.VAR _iv_mod_fm_15 = 25;
.VAR _iv_mod_fm_16 = 16;
.VAR _iv_mod_fm_17 = 0;
.VAR _iv_mod_fm_18 = 200;
.VAR _iv_mod_fm_19 = 0xffffffff;
.VAR _iv_mod_fm_20 = 21;
.VAR _iv_mod_fm_21 = 16;
.VAR _iv_mod_fm_22 = 0;
.VAR _iv_mod_fm_23 = 127;
.VAR _iv_mod_fm_24 = 0xffffffff;
.VAR _iv_mod_fm_25 = 24;
.VAR _iv_mod_fm_26 = 16;
.VAR _iv_mod_fm_27 = 0;
.VAR _iv_mod_fm_28 = 127;
.VAR _iv_mod_fm_29 = 0xffffffff;
