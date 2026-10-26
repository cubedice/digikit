// DT2 1.17 per-audio-frame position receiver. No allocation, calls, DO loops,
// UI work, or acknowledged-link traffic. The current RX ring is I3.
// Hook: stock block handler +0x34 (0x1c7501), before command dispatch/render.
// All touched registers and arithmetic status are saved explicitly; each
// save slot is 8 bytes, so a live PEYEN bit also saves/restores the paired S
// register without overwriting the next slot. The original MODE1 is restored
// before the R-register reloads. The patchkit tail executes the displaced
// I4 load. This routine is non-reentrant.
//
// Wire words 0..15 at receive byte 0xa7c:
// 0 PPOS magic, 1 version, 2 CF transmit sequence, 3 event sequence,
// 4 observed epoch, 5 flags (bit 0 valid, bit 1 running), 6 opaque pattern,
// 7 native master step, 8 length (0 = unknown), 9 substep, 10 snapshot CF block,
// 11 raw scheduler clock, 12 transmit CF block, 13 next step, 14 raw transport,
// 15 XOR of words 0..14 and PP_CHECK_SALT. Semantics belong to the CF hook.
// DSP record: wire mirror plus 16 receive count, 17 rejected frames,
// 18 accepted frames, 19 unsigned transmit-sequence delta (0 on first).
// Counter domains are distinct: neither is core/fx's conditional _fx_blk.
// Bad header/check or a repeated/backward transmit sequence clears magic and flags.
// An accepted wire with flags.valid=0 remains semantically invalid to clients.

#define PP_MAGIC 0x50504f53
#define PP_VERSION 1
#define PP_SUFFIX 0xa7c
#define PP_CHECK_SALT 0x7b91d6e3

.SECTION/PM seg_pmco;
.GLOBAL _pp_block_entry;
.EXTERN _pp_block_back;

_pp_block_entry:
    DM(_pp_sv_r0) = R0;
    DM(_pp_sv_r1) = R1;
    DM(_pp_sv_r2) = R2;
    DM(_pp_sv_r3) = R3;
    DM(_pp_sv_r4) = R4;
    DM(_pp_sv_r5) = R5;
    R0 = I0;
    DM(_pp_sv_i0) = R0;
    R0 = I1;
    DM(_pp_sv_i1) = R0;
    R0 = L0;
    DM(_pp_sv_l0) = R0;
    R0 = L1;
    DM(_pp_sv_l1) = R0;
    R0 = MODE1;
    DM(_pp_sv_mode1) = R0;
    R0 = ASTATX;
    DM(_pp_sv_astatx) = R0;
    R0 = ASTATY;
    DM(_pp_sv_astaty) = R0;
    R0 = STKYX;
    DM(_pp_sv_stkyx) = R0;
    R0 = STKYY;
    DM(_pp_sv_stkyy) = R0;
    BIT CLR MODE1 0x202000;             // PEYEN and ALUSAT; preserve the caller's other mode bits
    L0 = 0;
    L1 = 0;
    R0 = DM(_pattern_position_info + 64);
    R1 = R0 + 1;
    DM(_pattern_position_info + 64) = R1;
    R0 = I3;
    R1 = PP_SUFFIX;
    R2 = R0 + R1;
    I0 = R2;
    R0 = DM(0, I0);
    R1 = PP_MAGIC;
    COMP(R0, R1);
    IF NE JUMP _pp_bad;
    R0 = DM(1, I0);
    R1 = PP_VERSION;
    COMP(R0, R1);
    IF NE JUMP _pp_bad;
    I0 = R2;
    R3 = PP_CHECK_SALT;
    R0 = DM(0, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(1, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(2, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(3, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(4, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(5, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(6, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(7, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(8, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(9, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(10, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(11, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(12, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(13, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(14, I0);
    R4 = R3 XOR R0;
    R3 = R4;
    R0 = DM(15, I0);                   // word 15, checksum
    COMP(R0, R3);
    IF NE JUMP _pp_bad;
    I0 = R2;
    R0 = DM(2, I0);                    // this frame's transmit sequence
    R1 = DM(_pp_seen);
    R1 = PASS R1;
    IF EQ JUMP _pp_first;
    R1 = DM(_pattern_position_info + 8);
    R3 = R0 - R1;                      // unsigned wrap is intentional
    R3 = PASS R3;
    IF EQ JUMP _pp_bad;                // an old queue snapshot is not a new timing frame
    IF LT JUMP _pp_bad;                // serial arithmetic: reject a backward half-range
    JUMP _pp_copy;
_pp_first:
    R3 = 0;
    R0 = 1;
    DM(_pp_seen) = R0;
_pp_copy:
    DM(_pattern_position_info + 76) = R3;
    R0 = 0;
    DM(_pattern_position_info) = R0;    // publish magic last
    I0 = R2;
    I1 = _pattern_position_info;
    R0 = DM(1, I0);
    DM(1, I1) = R0;
    R0 = DM(2, I0);
    DM(2, I1) = R0;
    R0 = DM(3, I0);
    DM(3, I1) = R0;
    R0 = DM(4, I0);
    DM(4, I1) = R0;
    R0 = DM(5, I0);
    DM(5, I1) = R0;
    R0 = DM(6, I0);
    DM(6, I1) = R0;
    R0 = DM(7, I0);
    DM(7, I1) = R0;
    R0 = DM(8, I0);
    DM(8, I1) = R0;
    R0 = DM(9, I0);
    DM(9, I1) = R0;
    R0 = DM(10, I0);
    DM(10, I1) = R0;
    R0 = DM(11, I0);
    DM(11, I1) = R0;
    R0 = DM(12, I0);
    DM(12, I1) = R0;
    R0 = DM(13, I0);
    DM(13, I1) = R0;
    R0 = DM(14, I0);
    DM(14, I1) = R0;
    R0 = DM(15, I0);
    DM(15, I1) = R0;
    R0 = DM(_pattern_position_info + 72);
    R1 = R0 + 1;
    DM(_pattern_position_info + 72) = R1;
    R0 = PP_MAGIC;
    DM(_pattern_position_info) = R0;
    JUMP _pp_restore;
_pp_bad:
    R0 = DM(_pattern_position_info + 68);
    R1 = R0 + 1;
    DM(_pattern_position_info + 68) = R1;
    R0 = 0;
    DM(_pattern_position_info) = R0;
    DM(_pattern_position_info + 20) = R0;
_pp_restore:
    R0 = DM(_pp_sv_i0);
    I0 = R0;
    R0 = DM(_pp_sv_i1);
    I1 = R0;
    R0 = DM(_pp_sv_l0);
    L0 = R0;
    R0 = DM(_pp_sv_l1);
    L1 = R0;
    R0 = DM(_pp_sv_mode1);
    MODE1 = R0;
    R0 = DM(_pp_sv_stkyx);
    STKYX = R0;
    R0 = DM(_pp_sv_stkyy);
    STKYY = R0;
    R0 = DM(_pp_sv_astaty);
    ASTATY = R0;
    R0 = DM(_pp_sv_astatx);
    ASTATX = R0;
    R1 = DM(_pp_sv_r1);
    R2 = DM(_pp_sv_r2);
    R3 = DM(_pp_sv_r3);
    R4 = DM(_pp_sv_r4);
    R5 = DM(_pp_sv_r5);
    R0 = DM(_pp_sv_r0);
    JUMP _pp_block_back;

.PKDATA _pattern_position_info, 80;
.PKDATA _pp_seen, 4;
.PKDATA _pp_sv_r0, 8;
.PKDATA _pp_sv_r1, 8;
.PKDATA _pp_sv_r2, 8;
.PKDATA _pp_sv_r3, 8;
.PKDATA _pp_sv_r4, 8;
.PKDATA _pp_sv_r5, 8;
.PKDATA _pp_sv_i0, 8;
.PKDATA _pp_sv_i1, 8;
.PKDATA _pp_sv_l0, 8;
.PKDATA _pp_sv_l1, 8;
.PKDATA _pp_sv_mode1, 8;
.PKDATA _pp_sv_astatx, 8;
.PKDATA _pp_sv_astaty, 8;
.PKDATA _pp_sv_stkyx, 8;
.PKDATA _pp_sv_stkyy, 8;
