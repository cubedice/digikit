// DTII OVL3 carrier: the established spare cells +0x44/+0x48 in rows
// 17..20 and 29..31. ACK owns row0, diagnostics1..16, instruments21..28.
// Carrier word 7 checks the snapshot. The engine keeps its OVL1 mailbox.
// FX breadcrumbs write both reply rings immediately, not only at the tail;
// their row13 word297 must never alias the loop magic again.
#include "dk_loop.inc"

#define DL_LOOP_EMIT(SRC, DST) \
    R0 = DM(SRC, I3); \
    DM(DST, I2) = R0; \
    R3 = FEXT R5 BY 27:LS_WIRE_ROT; \
    R5 = LSHIFT R5 BY LS_WIRE_ROT; \
    R5 = R5 OR R3; \
    R5 = R5 XOR R0

.SECTION/PM seg_pmco;
.EXTERN _loop_ui_status;
_dl_loop_status:
    R0 = _loop_ui_status;
    R0 = PASS R0;
    IF EQ JUMP _dl_loop_status_return;
    I3 = R0;
    R0 = DM(LS_MAGIC, I3);
    R3 = LS_MAGIC_VALUE;
    COMP(R0, R3);
    IF NE JUMP _dl_loop_status_return;
    R3 = I4;
    I2 = R3;
    R5 = LS_WIRE_MAGIC;
    DL_LOOP_EMIT(LS_HEARTBEAT, 382);
    DL_LOOP_EMIT(LS_STATE, 402);
    DL_LOOP_EMIT(LS_LOOP_FRAMES, 403);
    DL_LOOP_EMIT(LS_NEXT_FRAMES, 423);
    DL_LOOP_EMIT(LS_PHASE, 424);
    DL_LOOP_EMIT(LS_GAIN, 444);
    DL_LOOP_EMIT(LS_WAVE_T, 633);
    DL_LOOP_EMIT(LS_STEPS, 634);
    DL_LOOP_EMIT(LS_CAPACITY, 654);
    DL_LOOP_EMIT(LS_WAVE_PEAK, 655);
    DL_LOOP_EMIT(LS_WAVE_EDGE, 675);
    DL_LOOP_EMIT(LS_WAVE_SILENT, 676);
    DM(445, I2) = R5;
    R0 = LS_WIRE_MAGIC; DM(381, I2) = R0;
    JUMP _dl_loop_status_return;
