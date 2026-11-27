// core/dsp-voice, DT2: the hook stubs of the SHARC engine (docs/findings/06, "Per-voice render hook"; R12-8 of docs/plans/runtime-and-services.md).
//
// The image keeps the two hook sites and everything the dispatch code and the machine modules share: the voice registry, the diagnostic block, the save words,
// the per-track state and the helper routines. The code that decides what runs (the mask scan, the contract adapter, the output guard) is dsp-voice-dispatch.s.
//
// Each stub reads one slot of core/dsp-link's slot block (`_dl_slots`; sdk/dsp/contract/dk_stub.inc):
//   slot 0: no dispatch code is installed. The stub runs the displaced instruction and the stock path, so the image renders exactly the stock audio.
//   else:   the pc of the dispatch entry. The stub saves the register it pins in an image word, pins M13 = 0 and jumps with `JUMP (M13, I12)`. The entry restores it.
//
// Render hook. patchkit overwrites the first instruction of the render entry, `MODIFY(I7, -26) (NW)`, with `JUMP _dv_entry`. At _dv_entry the stock contract holds:
// R4 = voice record, R8 = the voice's output buffer, R12 = 32, R14 = the voice loop counter (32 on the first voice, 1 on the last), I6 = the frame the caller's CJUMP built.
// R0 and R1 are free; I12 is free (the stock path assigns it before it reads it). M13 is the context's and is not guaranteed to be 0 (docs/findings/45).
// Voice 0 (R14 = 32) writes the heartbeat and the context registers as the firmware left them (finding 45, dl_diag) with absolute stores, so that no address register is used.
//
// Unpack hook (DT2). The frame consumer's two calls of the per-track unpack (R12 = track, R8 = voice) come to _dv_unpack. R0 is free there; the stub also saves I12.

//
// Stock chords (docs/findings/375-stock-voice-chords.md). A voice whose bit is set in `_cs_vmask`, on a track whose machine is a stock one, is rendered by
// `_cs_voice`: the owner as the caller would have rendered it, then up to three copies of its voice record at other pitches with the stock renderer, summed
// into the owner's output block before the stages that follow (bit reduction, filter, amp envelope, pan). The copies are taken at the owner's note-on, or
// as soon as the chord's shape arrives, and follow the owner's step. The track keyboard sends one track's shape per block in a word of the frame
// (`_cs_receive`, called by its pool receiver). A copy whose block holds a sample that is not finite or exceeds 8.0 in magnitude ends and is not added
// (`_cs_faults` counts them).

#include "dsp/contract/call.inc"
#include "adapter-dt2.inc"
#include "dk_machine.inc"
#include "dk_stub.inc"

// (literals only: the assembler takes the first term of an expression such as 3 * 0x1d8 and drops the rest)
#define CS_REC_BYTES 0x1d8                // a stock voice record
#define CS_REC_WORDS 118
#define CS_COPIES 3                       // copies per voice
#define CS_VOICE_BYTES 0x588              // CS_COPIES records: the copies of one voice
#define CS_MAGIC 0xc5                     // the top byte of a shape word
#define CS_STOCK_TYPES 7                  // machine types below this one are the engine's own
#define CS_LIMIT 0x41000000               // 8.0: the largest magnitude a copy's sample may have

.SECTION/PM seg_pmco;
.GLOBAL _dv_entry;
.GLOBAL _dv_unpack;
.GLOBAL _dv_idle;
.GLOBAL _dv_slots;
.GLOBAL _cs_receive;
.EXTERN _fw_render_next;
.EXTERN _fw_type_cache;
.EXTERN _fw_unpack;
.EXTERN _fw_frame;
.EXTERN _dl_slots;
.EXTERN _instrument_voice_rows;

_dv_unpack:
    R0 = DM(_dl_slots + DK_SLOT_VOICE_UNPACK);
    R0 = PASS R0;
    IF EQ JUMP _fw_unpack;
    DM(_dv_sv_m13u) = M13;
    DM(_dv_sv_i12u) = I12;
    M13 = 0;
    I12 = R0;
    JUMP (M13, I12);

_dv_entry:
    R0 = 32;
    COMP(R0, R14);
    IF NE JUMP _dv_slot;
    // once per block (voice 0): the heartbeat and the context registers as the firmware left them (finding 45, dl_diag)
    R1 = DM(_dv_diag);
    R1 = R1 + 1;
    DM(_dv_diag) = R1;
    // The optional trusted V2 table of core/instrument-play (rows of the registry's form): its address for the dispatch code, 0 when that module is not in the
    // image. The dispatch code is a service file and cannot name an optional symbol of the image, so the image hands it the address in a word.
    R1 = _instrument_voice_rows;
    DM(_dv_v2_rows) = R1;
    R1 = M5;
    DM(_dv_diag + 4) = R1;
    R1 = M6;
    DM(_dv_diag + 8) = R1;
    R1 = M7;
    DM(_dv_diag + 12) = R1;
    R1 = M13;
    DM(_dv_diag + 16) = R1;
    R1 = M14;
    DM(_dv_diag + 20) = R1;
    R1 = M15;
    DM(_dv_diag + 24) = R1;
    R1 = I6;
    DM(_dv_diag + 28) = R1;
    R1 = I7;
    DM(_dv_diag + 32) = R1;
    R1 = B7;
    DM(_dv_diag + 36) = R1;
    R1 = L7;
    DM(_dv_diag + 40) = R1;
    R1 = MODE1;
    DM(_dv_diag + 44) = R1;
_dv_slot:
    // A chord voice (bit v of _cs_vmask) goes to _cs_voice; with no bit set this costs three instructions per voice.
    R0 = DM(_cs_vmask);
    R0 = PASS R0;
    IF NE JUMP _cs_test;
_dv_slot2:
    R0 = DM(_dl_slots + DK_SLOT_VOICE_RENDER);
    R0 = PASS R0;
    IF EQ JUMP _dv_idle;
    DM(_dv_sv_m13) = M13;
    M13 = 0;
    I12 = R0;
    JUMP (M13, I12);

_dv_idle:
    MODIFY(I7, -26) (NW);                 // the displaced first instruction of the stock renderer
    JUMP _fw_render_next;

_cs_test:
    R1 = 32;
    R1 = R14 - R1;                        // -v
    R0 = LSHIFT R0 BY R1;                 // bit v to bit 0
    R1 = 1;
    R0 = R0 AND R1;
    IF EQ JUMP _dv_slot2;
    // Only a stock machine (a type below CS_STOCK_TYPES): a run-time machine renders with its own routine. STRETCH never reaches this hook.
    R1 = 32;
    R1 = R1 - R14;                        // v
    R2 = FEXT R1 BY 1:4;                  // t
    R1 = 0xe;
    R1 = R2 AND R1;
    R1 = LSHIFT R1 BY 1;                  // the byte offset of the word that holds cache[t] and cache[t ^ 1]
    R0 = _fw_type_cache;
    R1 = R0 + R1;
    I4 = R1;
    R1 = DM(0, I4);
    R2 = FEXT R2 BY 0:1;
    R2 = LSHIFT R2 BY 4;
    R0 = 0x400;
    R2 = R2 OR R0;
    R1 = FEXT R1 BY R2;                   // cache[t]
    R0 = CS_STOCK_TYPES;
    COMP(R1, R0);
    IF GE JUMP _dv_slot2;

// The chord routine, in place of the renderer (the stock contract: R4 = record, R8 = output block, R12 = the caller's tick, R14 = the voice loop counter,
// I6 = the frame of the caller's CJUMP). The row of track t in `_cs_cfg` (6 words): n copies (0 .. 3), gain (float; 0 leaves the sum as it is), the ratios
// 1 .. 3 (float, the pitch of copy e over the owner's), the shape word it came from. The copies of voice v are `_cs_rec` + (3 v + e) * 0x1d8.
// Callee-saved registers it uses are saved in its frame; the context's L1 and L4 are put back before each call of firmware code.
_cs_voice:
    DK_ENTER(16);
    DK_SAVE(2, R3);
    DK_SAVE(3, R5);
    DK_SAVE(4, R6);
    DK_SAVE(5, R7);
    DK_SAVE(6, R9);
    DK_SAVE(7, R10);
    DK_SAVE(8, R11);
    DK_SAVE(9, R13);
    R2 = I1;
    DK_SAVE(10, R2);
    R2 = L1;
    DK_SAVE(11, R2);
    R2 = L4;
    DK_SAVE(12, R2);
    DK_SAVE(13, R4);
    DK_SAVE(14, R8);
    DK_SAVE(15, R12);
    L1 = 0;
    L4 = 0;
    R13 = 32;
    R13 = R13 - R14;                      // v
    R9 = LSHIFT R13 BY -1;                // t
    R0 = 24;
    R0 = R9 * R0 (SSI);
    R1 = _cs_cfg;
    R5 = R0 + R1;                         // the track's row
    I4 = R5;
    R10 = DM(0, I4);                      // n
    R3 = DM(1, I4);                       // gain
    R0 = 0;
    R10 = MAX(R10, R0);
    R0 = CS_COPIES;
    R10 = MIN(R10, R0);
    R0 = 8;
    R5 = R5 + R0;                         // the ratios
    R0 = CS_VOICE_BYTES;
    R0 = R13 * R0 (SSI);
    R1 = _cs_rec;
    R6 = R0 + R1;                         // copy 0 of this voice
    // The owner's note-on: its sample pointer (+0) or phase (+0x1b0, Q31 int64) is not what its last render left (`_cs_last`, 4 words per voice): the
    // firmware's arm seeded them, and the copies of the last note are stale (word 3, the copies taken for the note, goes to 0). The record's own
    // note-on mark (+0x1b9) cannot serve: the stock renderer ends a voice whose mark is set.
    R0 = LSHIFT R13 BY 4;
    R1 = _cs_last;
    R8 = R0 + R1;
    I1 = R8;
    I4 = R4;
    R0 = DM(0, I4);
    R1 = DM(0, I1);
    COMP(R0, R1);
    IF NE JUMP _cs_on;
    R0 = DM(108, I4);
    R1 = DM(1, I1);
    COMP(R0, R1);
    IF NE JUMP _cs_on;
    R0 = DM(109, I4);
    R1 = DM(2, I1);
    COMP(R0, R1);
    IF EQ JUMP _cs_take;
_cs_on:
    R0 = 0;
    DM(3, I1) = R0;
_cs_take:
    // The copies the note does not have yet, while the owner sounds: all of them after its note-on, or those of a shape that came after it.
    MODIFY(I4, ADM_RECORD_ACTIVE);
    R0 = DM(0, I4);
    R0 = FEXT R0 BY 0:8;
    R0 = PASS R0;
    IF EQ JUMP _cs_steps;
    R7 = DM(3, I1);
_cs_cpn:
    COMP(R7, R10);
    IF GE JUMP _cs_tkn;
    R0 = CS_REC_BYTES;
    R0 = R7 * R0 (SSI);
    R0 = R0 + R6;
    I1 = R0;
    I4 = R4;
    // (the record addresses are byte addresses: a post-modify by M6 would step one byte, MODIFY (NW) steps a word)
    LCNTR = CS_REC_WORDS, DO _cs_cpw UNTIL LCE;
    R0 = DM(0, I4);
    DM(0, I1) = R0;
    MODIFY(I4, 1) (NW);
_cs_cpw: MODIFY(I1, 1) (NW);
    R7 = R7 + 1;
    JUMP _cs_cpn;
_cs_tkn:
    I1 = R8;
    DM(3, I1) = R7;
_cs_steps:
    // Every block: the step of copy e (+0x1a8, a signed Q31 int64, low word first) is the owner's step times ratio e, so the owner's pitch changes carry over.
    R7 = 0;
_cs_stl:
    COMP(R7, R10);
    IF GE JUMP _cs_render;
    R0 = CS_REC_BYTES;
    R0 = R7 * R0 (SSI);
    R0 = R0 + R6;
    I1 = R0;
    R0 = LSHIFT R7 BY 2;
    R0 = R0 + R5;
    I4 = R0;
    R12 = DM(0, I4);                      // ratio e
    I4 = R4;
    R0 = DM(106, I4);                     // the owner's step, low word
    R1 = DM(107, I4);                     // high word
    R0 = LSHIFT R0 BY -1;
    F0 = FLOAT R0;
    R2 = 1;
    F0 = SCALB F0 BY R2;
    F1 = FLOAT R1;
    R2 = 32;
    F1 = SCALB F1 BY R2;
    F0 = F0 + F1;
    F0 = F0 * F12;
    R2 = -32;
    F1 = SCALB F0 BY R2;
    R1 = TRUNC F1;                        // the new high word (toward zero)
    F2 = FLOAT R1;
    R2 = 32;
    F2 = SCALB F2 BY R2;
    F0 = F0 - F2;                         // what the low word holds
    IF GE JUMP _cs_stp;
    R1 = R1 - 1;
    R8 = 0x4f800000;                      // 2^32
    F0 = F0 + F8;
_cs_stp:
    R2 = -1;
    F0 = SCALB F0 BY R2;
    R0 = TRUNC F0;
    R0 = LSHIFT R0 BY 1;
    DM(106, I1) = R0;
    DM(107, I1) = R1;
    R7 = R7 + 1;
    JUMP _cs_stl;
_cs_render:
    DK_RESTORE(R0, 11);
    L1 = R0;
    DK_RESTORE(R0, 12);
    L4 = R0;
    DK_RESTORE(R4, 13);
    DK_RESTORE(R8, 14);
    DK_RESTORE(R12, 15);
    DK_CALL(_dv_slot2, _cs_r0);           // the owner, as the caller would have rendered it
    L1 = 0;
    L4 = 0;
    R0 = LSHIFT R13 BY 4;
    R1 = _cs_last;
    R0 = R0 + R1;
    I1 = R0;
    DK_RESTORE(R0, 13);
    I4 = R0;
    R0 = DM(0, I4);
    DM(0, I1) = R0;                       // what the owner's render left: its sample pointer and phase
    R0 = DM(108, I4);
    DM(1, I1) = R0;
    R0 = DM(109, I4);
    DM(2, I1) = R0;
    DK_RESTORE(R0, 11);
    L1 = R0;
    DK_RESTORE(R0, 12);
    L4 = R0;
    R7 = 0;
_cs_rl:
    COMP(R7, R10);
    IF GE JUMP _cs_gain;
    R0 = CS_REC_BYTES;
    R0 = R7 * R0 (SSI);
    R4 = R0 + R6;
    R8 = _cs_scratch;
    DK_RESTORE(R12, 15);
    DK_CALL(_dv_idle, _cs_r1);            // the stock renderer on copy e
    L1 = 0;
    L4 = 0;
    // The guard: every sample finite and at most 8.0 in magnitude (its bits without the sign at most those of 8.0). Otherwise the copy ends (ACTIVE
    // clear: the renderer zero-fills it from now on), is counted and its block is not added.
    I4 = _cs_scratch;
    R2 = 0;
    R8 = CS_LIMIT;
    R12 = 0x7fffffff;
    LCNTR = 32, DO _cs_gd UNTIL LCE;
    R0 = DM(0, I4);
    R0 = R0 AND R12;
    COMP(R0, R8);
    IF GT R2 = PASS R12;
_cs_gd: MODIFY(I4, 1) (NW);
    R2 = PASS R2;
    IF EQ JUMP _cs_sum;
    R0 = CS_REC_BYTES;
    R0 = R7 * R0 (SSI);
    R0 = R0 + R6;
    I4 = R0;
    R0 = DM(110, I4);                     // byte +0x1b8: ACTIVE
    R1 = 0xffffff00;
    R0 = R0 AND R1;
    DM(110, I4) = R0;
    R0 = DM(_cs_faults);
    R0 = R0 + 1;
    DM(_cs_faults) = R0;
    JUMP _cs_next;
_cs_sum:
    DK_RESTORE(R0, 14);
    I1 = R0;
    I4 = _cs_scratch;
    LCNTR = 32, DO _cs_add UNTIL LCE;
    F0 = DM(0, I4);
    F1 = DM(0, I1);
    F0 = F0 + F1;
    DM(0, I1) = F0;
    MODIFY(I4, 1) (NW);
_cs_add: MODIFY(I1, 1) (NW);
_cs_next:
    DK_RESTORE(R0, 11);
    L1 = R0;
    DK_RESTORE(R0, 12);
    L4 = R0;
    R7 = R7 + 1;
    JUMP _cs_rl;
_cs_gain:
    R3 = PASS R3;
    IF EQ JUMP _cs_out;
    L1 = 0;
    DK_RESTORE(R0, 14);
    I1 = R0;
    LCNTR = 32, DO _cs_gl UNTIL LCE;
    F0 = DM(0, I1);
    F0 = F0 * F3;
    DM(0, I1) = F0;
_cs_gl: MODIFY(I1, 1) (NW);
    DK_RESTORE(R0, 11);
    L1 = R0;
_cs_out:
    DK_RETADDR;
    DK_RESTORE(R3, 2);
    DK_RESTORE(R5, 3);
    DK_RESTORE(R6, 4);
    DK_RESTORE(R7, 5);
    DK_RESTORE(R9, 6);
    DK_RESTORE(R10, 7);
    DK_RESTORE(R11, 8);
    DK_RESTORE(R0, 10);
    I1 = R0;
    DK_RESTORE(R0, 11);
    L1 = R0;
    DK_RESTORE(R0, 12);
    L4 = R0;
    DK_LEAVE(R13, 9);

// The shape word of one track (R1; the track keyboard's frame word before its pool map, `_tk_pool_receive` calls this): CS_MAGIC in bits 24-31, the track
// in 20-23, the copies in 18-19 and three intervals in semitones (6 bits, signed) in 12-17, 6-11 and 0-5. A word the track's row already holds changes
// nothing; otherwise the row, its ratios and the track's two bits of `_cs_vmask` follow it. Uses R0-R5 and I0 (the receiver's).
_cs_receive:
    R0 = LSHIFT R1 BY -24;
    R2 = CS_MAGIC;
    COMP(R0, R2);
    IF NE RTS;
    R2 = FEXT R1 BY 20:4;                 // t
    R0 = 24;
    R0 = R2 * R0 (SSI);
    R3 = _cs_cfg;
    R0 = R0 + R3;
    I0 = R0;
    R3 = DM(5, I0);
    COMP(R1, R3);
    IF EQ RTS;
    DM(5, I0) = R1;
    R3 = FEXT R1 BY 18:2;                 // n
    DM(0, I0) = R3;
    R4 = LSHIFT R2 BY 1;
    R0 = 3;
    R0 = LSHIFT R0 BY R4;                 // voices 2t and 2t + 1
    R4 = DM(_cs_vmask);
    R5 = R4 OR R0;
    R0 = NOT R0;
    R4 = R4 AND R0;
    R3 = PASS R3;
    IF NE R4 = PASS R5;
    DM(_cs_vmask) = R4;
    R5 = R1;
    R4 = 14;
    CALL _cs_ratio;
    DM(2, I0) = F0;
    R4 = 20;
    CALL _cs_ratio;
    DM(3, I0) = F0;
    R4 = 26;
    CALL _cs_ratio;
    DM(4, I0) = F0;
    RTS;

// F0 = 2 ^ (i / 12) for the interval i of the shape word R5 that a left shift by R4 brings to bits 26-31. Clobbers R0-R3.
_cs_ratio:
    R0 = LSHIFT R5 BY R4;
    R1 = -26;
    R0 = ASHIFT R0 BY R1;
    F0 = FLOAT R0;
    R1 = 0x3daaaaab;                      // 1/12
    F0 = F0 * F1;
    JUMP _dv_exp2;                        // its RTS returns to the caller

// ---- helpers for machine routines (ordinary subroutines: CALL _dv_..., return with RTS) ----

// F0 = 2 ** F0 for F0 > -64 (relative error 8e-8: a degree-5 minimax polynomial of 2^f on [0, 1) and an exact
// scale by 2^floor). Clobbers R0-R3.
.GLOBAL _dv_exp2;
_dv_exp2:
    R1 = 0x42800000;                      // 64.0
    F0 = F0 + F1;
    R1 = TRUNC F0;
    F2 = FLOAT R1;
    F0 = F0 - F2;                         // f in [0, 1)
    R2 = 0x3af61942;
    F2 = F2 * F0;
    R3 = 0x3c1347f4;
    F2 = F2 + F3;
    F2 = F2 * F0;
    R3 = 0x3d64aa27;
    F2 = F2 + F3;
    F2 = F2 * F0;
    R3 = 0x3e75ead4;
    F2 = F2 + F3;
    F2 = F2 * F0;
    R3 = 0x3f31727b;
    F2 = F2 + F3;
    F2 = F2 * F0;
    R3 = 0x3f7fffff;
    F2 = F2 + F3;
    R3 = -64;
    R1 = R3 + R1;
    F0 = SCALB F2 BY R1;
#ifdef DV_C3_DB
    RTS (DB);
    NOP;
    NOP;
#else
    RTS;
#endif

// R0 = the unsigned 16-bit word k of the DSP's frame copy (R0 = k, 0..511); _dv_frame_sw sign-extends.
// Clobbers R0-R2, I4.
.GLOBAL _dv_frame_hw;
.GLOBAL _dv_frame_sw;
_dv_frame_hw:
    R1 = 0x1fe;
    R1 = R0 AND R1;
    R1 = LSHIFT R1 BY 1;
    R2 = _fw_frame;
    R1 = R2 + R1;
    I4 = R1;
    R1 = DM(0, I4);
    R2 = FEXT R0 BY 0:1;
    R2 = LSHIFT R2 BY 4;
    R0 = 0x400;                           // len6 = 16 at Ry[11:6], bit6 below it
    R2 = R2 OR R0;
    R0 = FEXT R1 BY R2;
#ifdef DV_C3_DB
    RTS (DB);
    NOP;
    NOP;
#else
    RTS;
#endif
_dv_frame_sw:
    R1 = 0x1fe;
    R1 = R0 AND R1;
    R1 = LSHIFT R1 BY 1;
    R2 = _fw_frame;
    R1 = R2 + R1;
    I4 = R1;
    R1 = DM(0, I4);
    R2 = FEXT R0 BY 0:1;
    R2 = LSHIFT R2 BY 4;
    R0 = 0x400;                           // len6 = 16 at Ry[11:6], bit6 below it
    R2 = R2 OR R0;
    R0 = FEXT R1 BY R2;
    R0 = LSHIFT R0 BY 16;
    R1 = -16;
    R0 = ASHIFT R0 BY R1;
#ifdef DV_C3_DB
    RTS (DB);
    NOP;
    NOP;
#else
    RTS;
#endif

.SECTION/DM seg_dmda;
// Stock chords: bit v marks a chord voice; 16 track rows of 6 words (n, gain, ratio 1 .. 3, the shape word); the scratch block; per voice the owner's
// sample pointer and phase after its last render and the copies taken for its note; the guard's count; the copies (32 voices x CS_COPIES x CS_REC_BYTES).
.PKDATA _cs_vmask, 4;
.PKDATA _cs_cfg, 384;
.PKDATA _cs_scratch, 128;
.PKDATA _cs_last, 512;
.PKDATA _cs_faults, 4;
.PKDATA _cs_rec, 45312;
.PKDATA _dv_sv_l4, 4;
.PKDATA _dv_sv_m13, 4;                     // the registers the stubs pinned (dk_stub.inc)
.PKDATA _dv_sv_m13u, 4;
.PKDATA _dv_sv_i12u, 4;
.PKDATA _dv_mask, 4;                      // registered types (see _dv_nodiag)
.PKDATA _dv_v2_rows, 4;                   // the address of the trusted V2 table (0: none), written by voice 0 each block (_dv_entry)
.PKDATA _dv_v2_scan, 4;                   // the dispatch code's: nonzero while a voice's scan is in the V2 table
.PKDATA _dv_sv3, 4;
.PKDATA _dv_sv5, 4;
.PKDATA _dv_bcw, 4;
// The guard's scalars and arrays (words): 0 row end, 1 type, 2 voice, 3 track, 4 voice record, 5 output block, 6 mask of clamped knobs, 7 halfword of knob 0,
// 8 fault kind, 9 faults, 10 disabled types, 11 to 18 the originals of the clamped words, 19 to 26 the words the routine saw.
.PKDATA _dv_g, 128;
// The diagnostic block (finding 45): word 0 counts the blocks that reached the hook, words 1-11 are M5, M6, M7, M13, M14, M15,
// I6, I7, B7, L7, MODE1 as the firmware left them at the first voice's entry; word 12 is the last breadcrumb, word 13 counts the
// dispatches to registered types, word 14 the routines that returned (finding 48). core/dsp-link copies the block into the record it sends to the ColdFire each block
// (dl_diag): its words 2 to 15.
.PKDATA _dv_diag, 128;
// The addresses of the two reply records (rings): [0] the one the last block built, [1] the one before it, and [2] the address of the module step word
// `_dk_step` (cleared before a module call). Written by core/dsp-link.
.PKDATA _dv_rec, 12;
// The contract adapter's data: the `dk_voice_in` it hands to the module, the parameter words (8), the blocks since the note-on per track (16), the gates
// per track (16), its scalars (see `_dv_contract`) and the knob positions of the module's parameters in order (ADM_PARAM_SLOT0 .. 7; 255: none).
.PKDATA _dv_in, DK_IN_BYTES;
.PKDATA _dv_params, 32;                    // live effective UI/debug words
.PKDATA _dv_call_params, 32;               // merged callback words (latched declarations over live words)
// Adapter-private snapshots: DT2 track / DN2 physical voice slot, never firmware-owned state.
.PKDATA _dv_trig_params, 512;
.PKDATA _dv_blk, 64;
.PKDATA _dv_gate, 64;
.PKDATA _dv_c, 64;
// Per track: nonzero while the even voice has written R into the odd voice's block this block (contract 1.2); the odd voice consumes it.
.PKDATA _dv_st, 64;
.VAR _dv_slots = ADM_PARAM_SLOT0;
.VAR _dv_slots1 = ADM_PARAM_SLOT1;
.VAR _dv_slots2 = ADM_PARAM_SLOT2;
.VAR _dv_slots3 = ADM_PARAM_SLOT3;
.VAR _dv_slots4 = ADM_PARAM_SLOT4;
.VAR _dv_slots5 = ADM_PARAM_SLOT5;
.VAR _dv_slots6 = ADM_PARAM_SLOT6;
.VAR _dv_slots7 = ADM_PARAM_SLOT7;
