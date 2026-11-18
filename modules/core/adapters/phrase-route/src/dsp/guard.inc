// core/phrase-route: the effect guard of the router. The router runs in every block, with or without an effect that owns the phrase, so it cannot depend on a
// service file being loaded: it carries its own copy of the routines that core/fx had in the image until the dispatch code moved into the `fx` service file
// (R12-8). The text below is the service's (modules/core/services/fx/dks/dsp/fx-chain.inc: `_fx_exec`, `_fx_fault`, `_fx_mark`, `_fx_enter`, `_fx_leave`,
// `_fx_zero`) with the code labels renamed `_phr_fx_*`; the data is core/fx's in the image, as there. Keep the two in step: after a change of fx-chain.inc
// in that range, copy it again (the header of this file and the label prefix are the only differences).
#ifndef FX_ROWS_INC
#define FX_ROWS_INC
#define FX_PC 0
#define FX_DESC 1
#define FX_STATE 2
#define FX_EN 3
#define FX_P0_BYTES 16
#define FX_WAS 12
#define FX_MIX 13
#define FX_DIS 14
#define FX_WSTART 15
#define FX_WCOUNT 16
#define FX_NFAULT 17
#define FX_ROUTE 18
#define FX_WINDOW 1500
#define FX_MAX_FAULTS 8
#endif

// Runs one effect row. In: I4 = the row (an effect: pc neither 0 nor 0xffffffff), `_fx_in` complete (`in` set by the caller). Does the decisions of the
// header comment up to the output check, but not the blend. Out: R0 = 1: the effect ran and its output block is in `_fx_scratch` (mode in `_fx_c + 4`,
// the enabled flag in `_fx_c + 8`; the caller blends or sends it and then sets `was` from `_fx_c + 8`), I4 = the row; R0 = 0: nothing to blend (the row is off,
// switched off by its faults, or its output failed the check, which is counted here). Called by the master loop and by the send hook (fxsend.s); the
// effect's process routine is entered with a JUMP, so the return address of the CALL stays on the PC stack.
_phr_fx_exec:
    R0 = DM(FX_DIS, I4);
    R0 = PASS R0;
    IF NE JUMP _phr_fx_xnone;
    R2 = DM(FX_EN, I4);
    R3 = DM(FX_WAS, I4);
    R4 = 1;
    R1 = 0;
    R2 = PASS R2;
    IF NE R1 = PASS R4;                   // enabled, 0 or 1
    DM(_fx_c + 8) = R1;
    COMP(R1, R3);
    IF NE JUMP _phr_fx_chg;
    R1 = PASS R1;
    IF EQ JUMP _phr_fx_xnone;                 // off and was off
    R5 = 0;                               // mode: steady
    JUMP _phr_fx_go;
_phr_fx_chg:
    R1 = PASS R1;
    IF EQ JUMP _phr_fx_off;
    CALL _phr_fx_zero;                        // switching on: a fresh state
    R5 = 1;                               // mode: fade in
    JUMP _phr_fx_go;
_phr_fx_off:
    R5 = 2;                               // mode: fade out
_phr_fx_go:
    DM(_fx_c + 4) = R5;
    R0 = DM(FX_ROUTE, I4);                // the site's beat: word 4 (master, route 0) or word 5 (send, route 1) of _fx_diag
    R0 = LSHIFT R0 BY 2;
    R3 = _fx_diag;
    R3 = R3 + R0;
    I1 = R3;
    R0 = DM(4, I1);
    R0 = R0 + 1;
    DM(4, I1) = R0;
    R5 = 4;                               // `last`: the core is about to call the module; the module step is cleared
    R6 = 0;
    CALL _phr_fx_mark;
    R4 = DM(FX_STATE, I4);
    R8 = _fx_in;
    R12 = I4;
    R0 = FX_P0_BYTES;
    R12 = R12 + R0;
    I12 = DM(FX_PC, I4);
    I13 = _phr_fx_ret;
    JUMP (M13, I12);                      // process returns with JUMP (M13, I13)
_phr_fx_ret:
    DM(_fx_c + 12) = R0;
    R0 = DM(_fx_c + 0);
    I4 = R0;
    R5 = 5;                               // `last`: the module returned (its step stays)
    R6 = -1;
    CALL _phr_fx_mark;
    R0 = DM(_fx_c + 12);
    R0 = PASS R0;
    IF LT JUMP _phr_fx_fault;                 // a negative result
    // the output block must hold finite samples of at most 8.0 in magnitude (the same integer test as the voice guard of core/dsp-voice)
    I0 = _fx_scratch;
    R13 = 0;
    R9 = 0x7fffffff;
    LCNTR = DK_FX_BLOCK, DO _phr_fx_chk UNTIL LCE;
        R0 = DM(I0, M6);
        R0 = R0 AND R9;
_phr_fx_chk: R13 = MAX(R13, R0);
    R0 = 0x41000000;                      // 8.0
    COMP(R13, R0);
    IF GT JUMP _phr_fx_fault;
    R0 = 1;                               // the output block is in _fx_scratch, I4 is still the row
    RTS;
_phr_fx_xnone:
    R0 = 0;
    RTS;

// A fault (I4 = the row): the master block is untouched, the state is zeroed, the effect counts as off. The 8th fault within the window sets `dis`.
_phr_fx_fault:
    CALL _phr_fx_zero;
    R0 = 0;
    DM(FX_WAS, I4) = R0;
    R1 = DM(_fx_diag + 4);
    R1 = R1 + 1;
    DM(_fx_diag + 4) = R1;                // faults of all effects
    R2 = I4;
    R3 = _fx_table;
    R2 = R2 - R3;
    R2 = R2 + 1;
    DM(_fx_diag + 8) = R2;                // the row (byte offset + 1) of the last fault
    R5 = DM(_fx_blk);
    R6 = DM(FX_WSTART, I4);
    R7 = R5 - R6;
    R0 = FX_WINDOW;
    COMP(R7, R0);
    IF LT JUMP _phr_fx_fwin;
    DM(FX_WSTART, I4) = R5;
    R7 = 0;
    DM(FX_WCOUNT, I4) = R7;
_phr_fx_fwin:
    R7 = DM(FX_WCOUNT, I4);
    R7 = R7 + 1;
    DM(FX_WCOUNT, I4) = R7;
    R9 = DM(FX_NFAULT, I4);
    R9 = R9 + 1;
    DM(FX_NFAULT, I4) = R9;
    R0 = FX_MAX_FAULTS;
    COMP(R7, R0);
    IF LT JUMP _phr_fx_xnone;
    R0 = 1;
    DM(FX_DIS, I4) = R0;
    DM(_fx_diag + 12) = R2;               // the row (byte offset + 1) switched off last
    JUMP _phr_fx_xnone;

// The core's mark of a module call (the fault table, dsp-link.inc): In: I4 = the row, R5 = stage (4 the module is about to be called, 5 it returned), R6 = the mask
// for the module step (0 clears it, -1 keeps it). The module step is the static word `_dk_step` (its address is _fx_rec[2], written by core/dsp-link; 0 without it):
// the mark clears it at stage 4, so a step belongs to the call that stored it, and the block tail copies it into the block's record. Stores `last` = stage | site << 8
// (2 master, 3 send: 2 + route) | row byte offset << 16 in _fx_diag word 0 and, at once, into both reply records (_fx_rec[0], [1]) with the header word, so that it is
// there when the DSP stops inside the module and never reaches the block tail (finding 48). Clobbers R0, R3, I1.
_phr_fx_mark:
    R3 = I4;
    R0 = _fx_table;
    R3 = R3 - R0;
    R3 = LSHIFT R3 BY 16;
    R0 = DM(FX_ROUTE, I4);
    R0 = BSET R0 BY 1;                    // 2 + route (the route is 0 or 1)
    R0 = LSHIFT R0 BY 8;
    R3 = R3 OR R0;
    R3 = R3 OR R5;
    DM(_fx_diag) = R3;
    I1 = _fx_rec;
    R0 = DM(2, I1);
    R0 = PASS R0;
    IF EQ JUMP _phr_fx_mk0;
    I1 = R0;
    R0 = DM(0, I1);
    R0 = R0 AND R6;
    DM(0, I1) = R0;
    I1 = _fx_rec;
_phr_fx_mk0:
    R0 = DM(0, I1);
    R0 = PASS R0;
    IF EQ JUMP _phr_fx_mk1;
    I1 = R0;
    DM(297, I1) = R3;
    R0 = 0x20D1A6;                        // DL_DG_MAGIC | 32 << 16: a record the engine had cleared still decodes
    DM(45, I1) = R0;
    I1 = _fx_rec;
_phr_fx_mk1:
    R0 = DM(1, I1);
    R0 = PASS R0;
    IF EQ RTS;
    I1 = R0;
    DM(297, I1) = R3;
    R0 = 0x20D1A6;
    DM(45, I1) = R0;
    RTS;

// Saves every register that the stock function expects back and the context registers, pins the context and sets the output and the
// block number (`_fx_blk`, not advanced here) of the dk_fx_in (the caller sets its `in`). Called by the master hook and by the send hook (fxsend.s). Returns with RTS; clobbers R0, R3, I4.
_phr_fx_enter:
    I4 = _fx_save;
    DM(0, I4) = R1;
    DM(1, I4) = R2;
    DM(2, I4) = R3;
    DM(3, I4) = R4;
    DM(4, I4) = R5;
    DM(5, I4) = R6;
    DM(6, I4) = R7;
    DM(7, I4) = R8;
    DM(8, I4) = R9;
    DM(9, I4) = R10;
    DM(10, I4) = R11;
    DM(11, I4) = R12;
    DM(12, I4) = R13;
    DM(13, I4) = R14;
    DM(14, I4) = R15;
    R0 = I0;                              // a DM store cannot take a DAG1 register (UREGXDAG1, PRM p.15-7): through R0 (saved in _fx_s_r0 by the stub)
    DM(15, I4) = R0;
    R0 = I1;
    DM(16, I4) = R0;
    R0 = I2;
    DM(17, I4) = R0;
    R0 = I3;
    DM(18, I4) = R0;
    R0 = I5;
    DM(19, I4) = R0;
    DM(20, I4) = I12;
    DM(21, I4) = I13;
    R0 = M1;
    DM(22, I4) = R0;
    R0 = M2;
    DM(23, I4) = R0;
    R0 = M3;
    DM(24, I4) = R0;
    R0 = M4;
    DM(25, I4) = R0;
    R0 = B3;
    DM(26, I4) = R0;
    // save the context registers, then pin them
    I4 = _fx_ctx;
    R3 = M5;
    DM(0, I4) = R3;
    R3 = M6;
    DM(1, I4) = R3;
    R3 = M7;
    DM(2, I4) = R3;
    DM(3, I4) = M13;
    DM(4, I4) = M14;
    DM(5, I4) = M15;
    R3 = L0;
    DM(6, I4) = R3;
    R3 = L1;
    DM(7, I4) = R3;
    R3 = L2;
    DM(8, I4) = R3;
    R3 = L3;
    DM(9, I4) = R3;
    R3 = L5;
    DM(10, I4) = R3;
    R3 = MODE1;
    DM(11, I4) = R3;
    M5 = 0;
    M6 = 1;
    M7 = -1;
    M13 = 0;
    M14 = 1;
    M15 = -1;
    L0 = 0;
    L1 = 0;
    L2 = 0;
    L3 = 0;
    L5 = 0;
    BIT CLR MODE1 0x20A000;               // PEYEN, TRUNCATE, ALUSAT
    BIT SET MODE1 0x10000;                // RND32
    NOP;

    // the block number and the dk_fx_in (the counter itself is advanced by the master hook only)
    R0 = DM(_fx_blk);
    I4 = _fx_in;
    R1 = _fx_scratch;
    DM(DK_FX_IN_W_OUT, I4) = R1;
    DM(DK_FX_IN_W_BLOCK, I4) = R0;
    RTS;

// Restores the context registers and every register that _phr_fx_enter saved (not L4, I4 and R0: the hook's tail does). Returns with RTS.
_phr_fx_leave:
    I4 = _fx_ctx;
    R0 = DM(11, I4);
    MODE1 = R0;
    NOP;
    M5 = DM(0, I4);
    M6 = DM(1, I4);
    M7 = DM(2, I4);
    M13 = DM(3, I4);
    M14 = DM(4, I4);
    M15 = DM(5, I4);
    L0 = DM(6, I4);
    L1 = DM(7, I4);
    L2 = DM(8, I4);
    L3 = DM(9, I4);
    L5 = DM(10, I4);
    I4 = _fx_save;
    B3 = DM(26, I4);                      // a B register load also loads its I register (PRM p.6-24): B3 before I3
    R1 = DM(0, I4);
    R2 = DM(1, I4);
    R3 = DM(2, I4);
    R4 = DM(3, I4);
    R5 = DM(4, I4);
    R6 = DM(5, I4);
    R7 = DM(6, I4);
    R8 = DM(7, I4);
    R9 = DM(8, I4);
    R10 = DM(9, I4);
    R11 = DM(10, I4);
    R12 = DM(11, I4);
    R13 = DM(12, I4);
    R14 = DM(13, I4);
    R15 = DM(14, I4);
    I0 = DM(15, I4);
    I1 = DM(16, I4);
    I2 = DM(17, I4);
    I3 = DM(18, I4);
    I5 = DM(19, I4);
    I12 = DM(20, I4);
    I13 = DM(21, I4);
    M1 = DM(22, I4);
    M2 = DM(23, I4);
    M3 = DM(24, I4);
    M4 = DM(25, I4);
    RTS;

// In: I4 = the row. Zeroes the effect's state (state_bytes of its desc). Clobbers R0, R5, I1, I2.
_phr_fx_zero:
    I1 = DM(FX_DESC, I4);
    R5 = DM(DK_FX_DESC_W_STATE_BYTES, I1);
    R0 = -2;
    R5 = LSHIFT R5 BY R0;                 // words
    R5 = PASS R5;
    IF EQ RTS;
    I2 = DM(FX_STATE, I4);
    R0 = 0;
    LCNTR = R5, DO _phr_fx_zl UNTIL LCE;
_phr_fx_zl: DM(I2, M6) = R0;
    RTS;
