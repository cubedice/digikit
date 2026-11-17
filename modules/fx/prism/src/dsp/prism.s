// PRISM: bounded phrase capture and granular playback. Audio and controls come
// exclusively from versioned providers; no stock address is embedded here.
#include "dk_fx.inc"
#include "dk_phrase_imports.inc"
#include "prism.h"

.SECTION/PM seg_pmco;
.GLOBAL _prism_process;
.EXTERN _prism_capture;
.EXTERN _prism_grain;
.EXTERN _prism_space;

_prism_process:
    I5 = R4;
    I4 = R8;
    R0 = DM(DK_FX_IN_W_IN,I4);
    DM(PS_INPUT,I5) = R0;
    R0 = DM(DK_FX_IN_W_OUT,I4);
    DM(PS_OUTPUT,I5) = R0;
    R0 = DM(DK_FX_IN_W_BLOCK,I4);
    DM(PS_BLOCK,I5) = R0;
    I4 = _phrase_control_info;
    R0 = DM(PH_C_MAGIC / 4,I4);
    R1 = PH_CONTROL_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_VERSION / 4,I4);
    R1 = PH_VERSION;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_SIZE / 4,I4);
    R1 = PH_CONTROL_WORDS * 4;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    R0 = DM(PH_C_ENABLED / 4,I4);
    R0 = PASS R0;
    IF EQ JUMP _prism_bad_link;
    R6 = DM(PH_C_SEQ / 4,I4);
    R0 = DM(PH_C_SEQ_END / 4,I4);
    COMP(R6,R0);
    IF NE JUMP _prism_bad_link;
    // Snapshot is immutable for this call. The provider updates before the FX
    // callback; the second sequence read rejects a mixed publication.
    R0 = I5;
    R1 = PS_CONTROL * 4;
    R2 = R0 + R1;
    I3 = R2;
    LCNTR = PH_CONTROL_WORDS, DO _prism_control_copy UNTIL LCE;
        R0 = DM(I4,M6);
_prism_control_copy: DM(I3,M6) = R0;
    I4 = _phrase_control_info;
    R0 = DM(PH_C_SEQ / 4,I4);
    COMP(R0,R6);
    IF NE JUMP _prism_bad_link;
    DM(PS_CONTROL_SEQ,I5) = R6;
    // Reject non-finite control payloads before they reach a float-to-index
    // conversion. Integer controls in this span have small finite bit patterns.
    R0 = I5;
    R1 = (PS_CONTROL * 4 + PH_C_FADE);
    R2 = R0 + R1;
    I3 = R2;
    R3 = 17;
    R1 = 0x7f800000;
_prism_finite_control:
    R0 = DM(0,I3);
    R2 = R0 AND R1;
    COMP(R2,R1);
    IF EQ JUMP _prism_bad_link;
    MODIFY(I3,1) (NW);
    R3 = R3 - 1;
    IF GT JUMP _prism_finite_control;

    I4 = _phrase_storage_info;
    R0 = DM(PH_STORAGE_MAGIC_OFF / 4,I4);
    R1 = PH_STORAGE_MAGIC;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PH_STORAGE_VERSION / 4,I4);
    R1 = PH_VERSION;
    COMP(R0,R1);
    IF NE JUMP _prism_bad_storage;
    R6 = DM(PH_STORAGE_BANK0 / 4,I4);
    R7 = DM(PH_STORAGE_BANK1 / 4,I4);
    R8 = DM(PH_STORAGE_BANK_BYTES / 4,I4);
    R9 = DM(PH_STORAGE_FRAMES / 4,I4);
    R0 = 31;
    R1 = R9 AND R0;
    R1 = PASS R1;
    IF NE JUMP _prism_bad_storage;
    R0 = 64;
    COMP(R9,R0);
    IF LT JUMP _prism_bad_storage;
    R0 = 1032192;
    COMP(R9,R0);
    IF GT JUMP _prism_bad_storage;
    // A lease cannot move under active readers. Runtime reload supplies a
    // fresh zeroed state; an in-place descriptor mutation fails closed.
    R0 = DM(PS_INIT,I5);
    R0 = PASS R0;
    IF EQ JUMP _prism_storage_extent;
    R0 = DM(PS_BANK0,I5);
    COMP(R0,R6);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PS_BANK1,I5);
    COMP(R0,R7);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PS_CAPACITY,I5);
    COMP(R0,R9);
    IF NE JUMP _prism_bad_storage;
    R0 = DM(PS_BANK_BYTES,I5);
    COMP(R0,R8);
    IF NE JUMP _prism_bad_storage;
_prism_storage_extent:
    R0 = LSHIFT R9 BY 3;
    R1 = LSHIFT R9 BY -3;
    R2 = R0 + R1;
    R3 = 32;
    R1 = R2 + R3;
    COMP(R8,R1);
    IF LT JUMP _prism_bad_storage;
    DM(PS_STAMP_BYTES,I5) = R0;
    R0 = LSHIFT R9 BY -5;
    DM(PS_BLOCKS,I5) = R0;
    DM(PS_CAPACITY,I5) = R9;
    DM(PS_BANK_BYTES,I5) = R8;
    // Validate both regions in external memory without wrapping base+size.
    R0 = 3;
    R1 = R6 OR R7;
    R2 = R1 AND R0;
    R2 = PASS R2;
    IF NE JUMP _prism_bad_storage;
    R0 = 0x80000000;
    R1 = R6 XOR R0;
    R2 = R7 XOR R0;
    R3 = 0x20000000;
    R1 = PASS R1;
    IF LT JUMP _prism_bad_storage;
    R2 = PASS R2;
    IF LT JUMP _prism_bad_storage;
    COMP(R1,R3);
    IF GE JUMP _prism_bad_storage;
    COMP(R2,R3);
    IF GE JUMP _prism_bad_storage;
    R4 = R3 - R1;
    COMP(R8,R4);
    IF GT JUMP _prism_bad_storage;
    R4 = R3 - R2;
    COMP(R8,R4);
    IF GT JUMP _prism_bad_storage;
    R3 = R1 - R2;
    R3 = PASS R3;
    IF LT R3 = -R3;
    COMP(R3,R8);
    IF LT JUMP _prism_bad_storage;
    DM(PS_BANK0,I5) = R6;
    DM(PS_BANK1,I5) = R7;
    R0 = DM(PS_INIT,I5);
    R0 = PASS R0;
    IF EQ JUMP _prism_initialize;
    R0 = DM(PS_OWNER,I5);
    R1 = DM(PC_OWNER,I5);
    COMP(R0,R1);
    IF NE JUMP _prism_bad_link;
    JUMP _prism_run;
_prism_initialize:
    R0 = 1;
    DM(PS_INIT,I5) = R0;
    DM(PS_ROLL_BANK,I5) = R6;
    R0 = 0x6d2b79f5;
    DM(PS_RNG,I5) = R0;
    R0 = DM(PC_SELECTION,I5);
    DM(PS_SELECTION,I5) = R0;
    R0 = DM(PC_EPOCH,I5);
    DM(PS_EPOCH,I5) = R0;
    R0 = DM(PC_OWNER,I5);
    DM(PS_OWNER,I5) = R0;
    R0 = DM(PC_PHASE,I5);
    DM(PS_PREV_PHASE,I5) = R0;
_prism_run:
    CALL _prism_capture;
    CALL _prism_grain;
    CALL _prism_space;
    CALL _prism_publish;
    R0 = DK_FX_OK;
    JUMP (M13,I13);

_prism_bad_storage:
    R0 = PH_ERROR_STORAGE;
    JUMP _prism_fault;
_prism_bad_link:
    R0 = PH_ERROR_LINK;
_prism_fault:
    DM(PS_ERROR,I5) = R0;
    R0 = 0;
    DM(PS_HELD_MASK,I5) = R0;
    DM(PS_OLD_MASK,I5) = R0;
    DM(PS_HELD_FRAMES,I5) = R0;
    DM(PS_MODE,I5) = R0;
    CALL _prism_publish;
    R0 = -1;
    JUMP (M13,I13);

// Engine writes all fields except PH_S_FADE, owned by the routing adapter.
// Publish the sequence last. The same block's audio and source ownership are
// consumed together; absent/failed processing leaves no fresh heartbeat.
_prism_publish:
    I4 = _phrase_status;
    R0 = 0;
    DM(PH_S_SEQ_END / 4,I4) = R0;
    R0 = PH_STATUS_MAGIC;
    DM(PH_S_MAGIC / 4,I4) = R0;
    R0 = PH_VERSION;
    DM(PH_S_VERSION / 4,I4) = R0;
    R0 = DM(PS_BLOCK,I5);
    R0 = R0 + 1;
    DM(PH_S_HEARTBEAT / 4,I4) = R0;
    R1 = DM(PS_MODE,I5);
    R1 = PASS R1;
    IF NE JUMP _prism_status_state;
    R1 = PH_FILLING;
    R2 = DM(PS_HISTORY_READY,I5);
    R3 = PH_READY;
    R2 = PASS R2;
    IF NE R1 = PASS R3;
    R2 = DM(PS_HELD_FRAMES,I5);
    R3 = PH_HELD;
    R2 = PASS R2;
    IF NE R1 = PASS R3;
    R2 = DM(PS_HANDOFF,I5);
    R3 = PH_HANDOFF;
    R2 = PASS R2;
    IF NE R1 = PASS R3;
    R2 = DM(PC_MASK,I5);
    R3 = PH_EMPTY;
    R2 = PASS R2;
    IF NE JUMP _prism_status_state;
    R2 = DM(PS_HELD_FRAMES,I5);
    R2 = PASS R2;
    IF EQ R1 = PASS R3;
_prism_status_state:
    DM(PH_S_STATE / 4,I4) = R1;
    R1 = DM(PS_ERROR,I5);
    DM(PH_S_ERROR / 4,I4) = R1;
    R1 = DM(PS_HELD_MASK,I5);
    DM(PH_S_HELD_MASK / 4,I4) = R1;
    R1 = DM(PS_OLD_MASK,I5);
    DM(PH_S_OLD_MASK / 4,I4) = R1;
    R1 = DM(PS_HANDOFF,I5);
    F1 = FLOAT R1;
    R2 = 0x3d088889; // 1/30
    F3 = F1 * F2;
    R2 = 0x3f800000;
    F1 = F2 - F3;
    DM(PH_S_HANDOFF / 4,I4) = R1;
    R1 = DM(PS_HELD_FRAMES,I5);
    DM(PH_S_HELD_FRAMES / 4,I4) = R1;
    R1 = DM(PS_CAPTURE_BLOCKS,I5);
    R2 = LSHIFT R1 BY 5;
    DM(PH_S_CAPTURE_PROGRESS / 4,I4) = R2;
    R1 = DM(PS_VALID_BLOCKS,I5);
    R2 = LSHIFT R1 BY 5;
    DM(PH_S_HISTORY_FRAMES / 4,I4) = R2;
    R1 = DM(PS_ARM_GOAL,I5);
    R2 = LSHIFT R1 BY -16;
    DM(PH_S_GOAL_STEPS / 4,I4) = R2;
    R1 = DM(PS_CROP_START,I5);
    DM(PH_S_CROP_START / 4,I4) = R1;
    R1 = DM(PS_CROP_LENGTH,I5);
    DM(PH_S_CROP_LEN / 4,I4) = R1;
    R1 = DM(PS_SCAN,I5);
    DM(PH_S_SCAN_PHASE / 4,I4) = R1;
    R1 = DM(PS_ACTIVE,I5);
    DM(PH_S_ACTIVE_GRAINS / 4,I4) = R1;
    R1 = DM(PS_LIMIT,I5);
    DM(PH_S_LIMIT / 4,I4) = R1;
    R1 = DM(PS_ACK,I5);
    DM(PH_S_COMMAND_ACK / 4,I4) = R1;
    R1 = DM(PS_SELECTION,I5);
    DM(PH_S_SELECTION_EPOCH / 4,I4) = R1;
    R1 = DM(PS_BANK_GENERATION,I5);
    DM(PH_S_BANK_GENERATION / 4,I4) = R1;
    R1 = DM(PS_HELD_BANK,I5);
    DM(PH_S_HELD_BANK / 4,I4) = R1;
    R1 = DM(PS_OLD_BANK,I5);
    DM(PH_S_OLD_BANK / 4,I4) = R1;
    R1 = DM(PS_OWNER,I5);
    DM(PH_S_OWNER_GENERATION / 4,I4) = R1;
    R1 = DM(PS_MODE,I5);
    R2 = 0;
    R3 = 1;
    R1 = PASS R1;
    IF NE R2 = PASS R3;
    DM(PH_S_REC_ACTIVE / 4,I4) = R2;
    CALL _prism_origins;
    R0 = DM(PS_BLOCK,I5);
    R0 = R0 + 1;
    DM(PH_S_SEQ_END / 4,I4) = R0;
    RTS;

// Publish launch anchors against the full held phrase. Old-bank readers are
// intentionally absent: their positions belong to a different recording.
_prism_origins:
    R0 = I4;
    R1 = PH_S_GRAIN_POS0;
    R2 = R0 + R1;
    I2 = R2;
    R0 = 0;
    LCNTR = 5, DO _prism_origins_clear UNTIL LCE;
_prism_origins_clear: DM(I2,M6) = R0;
    R0 = DM(PS_HELD_FRAMES,I5);
    R0 = PASS R0;
    IF EQ RTS;
    F0 = FLOAT R0;
    F1 = RECIPS F0;
    F2 = F0 * F1;
    R3 = 0x40000000;
    F0 = F3 - F2;
    F10 = F0 * F1;
    R0 = DM(PS_CROP_LENGTH,I5);
    F8 = FLOAT R0;
    F0 = RECIPS F8;
    F1 = F0 * F8;
    F2 = F3 - F1;
    F9 = F0 * F2;
    R0 = DM(PS_CROP_START,I5);
    F11 = FLOAT R0;
    R0 = I5;
    R1 = PS_GRAINS * 4;
    R2 = R0 + R1;
    I3 = R2;
    R0 = I4;
    R1 = PH_S_GRAIN_POS0;
    R2 = R0 + R1;
    I2 = R2;
    R4 = 0;
    R5 = 0;
    R6 = 1;
    R7 = 0;
_prism_origin_next:
    R0 = DM(PG_REMAIN,I3);
    R0 = PASS R0;
    IF LE JUMP _prism_origin_pack;
    R0 = DM(PG_OLD,I3);
    R0 = PASS R0;
    IF NE JUMP _prism_origin_pack;
    R0 = R4 OR R6;
    R4 = R0;
    F0 = DM(PG_ORIGIN,I3);
    F1 = F0 * F9;
    R0 = TRUNC F1;
    F2 = FLOAT R0;
    F3 = F1 - F2;
    R0 = 0;
    COMP(F3,F0);
    IF GE JUMP _prism_origin_positive;
    R0 = 0x3f800000;
    F1 = F3 + F0;
    R3 = R1;
_prism_origin_positive:
    F0 = F3 * F8;
    F1 = F0 + F11;
    F2 = F1 * F10;
    R0 = 0x477fff00; // 65535 normalized positions across the held phrase
    F1 = F2 * F0;
    R0 = TRUNC F1;
    R1 = 65535;
    COMP(R0,R1);
    IF GT R0 = PASS R1;
    R1 = 0;
    COMP(R0,R1);
    IF LT R0 = PASS R1;
    R1 = 1;
    R2 = R7 AND R1;
    R2 = PASS R2;
    IF EQ JUMP _prism_origin_merge;
    R1 = LSHIFT R0 BY 16;
    R0 = R1;
_prism_origin_merge:
    R1 = R5 OR R0;
    R5 = R1;
_prism_origin_pack:
    R0 = 1;
    R1 = R7 AND R0;
    R1 = PASS R1;
    IF EQ JUMP _prism_origin_advance;
    DM(I2,M6) = R5;
    R5 = 0;
_prism_origin_advance:
    MODIFY(I3,PRISM_GRAIN_WORDS) (NW);
    R0 = LSHIFT R6 BY 1;
    R6 = R0;
    R7 = R7 + 1;
    R0 = PRISM_GRAINS;
    COMP(R7,R0);
    IF LT JUMP _prism_origin_next;
    DM(PH_S_GRAIN_VALID / 4,I4) = R4;
    RTS;

.SECTION/DM seg_dmda;
.GLOBAL _prism_desc;
.VAR _prism_desc = DK_FX_MAGIC;
.VAR _prism_desc1 = DK_FX_V1;
.VAR _prism_desc2 = 0;
.VAR _prism_desc3 = PRISM_STATE_BYTES;
.VAR _prism_desc4 = 48000;
.VAR _prism_desc5 = 3;
.VAR _prism_desc6 = 0;
