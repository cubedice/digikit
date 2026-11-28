// DTII OS 1.17. Receive the same owner map used to prepare the native frame.
// Called from the position receiver with PEYEN off and I3 at the RX ring.
// R0-R5, I0 and flags belong to that receiver. Bad packets keep the old
// route so a helper's tail cannot jump to its own sends or master channel.
// The word before the map is one track's stock-chord shape (finding 375): core/dsp-voice's _cs_receive takes it (0 without that module).
#define TK_CHORD_SUFFIX 0x850
#define TK_POOL_SUFFIX 0x854
#define TK_POOL_MAGIC 0x54
#define TK_POOL_SALT 0x22dca71b
#define TK_POOL_RELAY_OWNER 10
#define TK_TRACK_BUS 0x252df8

.SECTION/PM seg_pmco;
.GLOBAL _tk_pool_receive;
.EXTERN _cs_receive;
_tk_pool_receive:
    R0 = _cs_receive;
    R0 = PASS R0;
    IF EQ JUMP _tk_pool_map;
    R0 = I3;
    R1 = TK_CHORD_SUFFIX;
    R0 = R0 + R1;
    I0 = R0;
    R1 = DM(0,I0);
    CALL _cs_receive;
_tk_pool_map:
    R0 = I3;
    R1 = TK_POOL_SUFFIX;
    R0 = R0 + R1;
    I0 = R0;
    R1 = DM(0,I0);
    R2 = LSHIFT R1 BY -24;
    R3 = TK_POOL_MAGIC;
    COMP(R2,R3);
    IF NE RTS;
    R2 = DM(1,I0);
    R3 = DM(2,I0);
    R4 = DM(3,I0);
    R4 = R4 XOR R1;
    R4 = R4 XOR R2;
    R4 = R4 XOR R3;
    R5 = TK_POOL_SALT;
    COMP(R4,R5);
    IF NE RTS;
    R1 = FEXT R1 BY 0:16;
    R0 = PASS R1;
    IF EQ JUMP _tk_pool_publish;

    // Reject a map that would send a helper through another helper.
    R4 = PASS R2;
    R5 = PASS R3;
_tk_pool_validate:
    BTST R0 BY 0;
    IF NOT SZ JUMP _tk_pool_binding;
    JUMP _tk_pool_advance;
_tk_pool_binding:
    DM(_tk_pool_rx_r6) = R6;
    R6 = FEXT R4 BY 0:4;
    R6 = -R6;
    R6 = LSHIFT R1 BY R6;
    BTST R6 BY 0;
    R6 = DM(_tk_pool_rx_r6);
    IF NOT SZ RTS;
_tk_pool_advance:
    R0 = LSHIFT R0 BY -1;
    R0 = PASS R0;
    IF EQ JUMP _tk_pool_publish;
    DM(_tk_pool_rx_r6) = R6;
    R6 = LSHIFT R5 BY 28;
    R4 = LSHIFT R4 BY -4;
    R4 = R4 OR R6;
    R5 = LSHIFT R5 BY -4;
    R6 = DM(_tk_pool_rx_r6);
    JUMP _tk_pool_validate;
_tk_pool_publish:
    DM(_tk_pool_owners_low) = R2;
    DM(_tk_pool_owners_high) = R3;
    DM(_tk_pool_mask) = R1;
    R0 = DM(0,I0);
    R0 = FEXT R0 BY 16:8;
    R4 = 0x4b;
    R0 = R0 XOR R4;
    DM(_tk_pool_tag) = R0;
    RTS;

// R1: physical voice. Return its authenticated RELAY group owner or -1.
// Ordinary sample groups leave the non-helper owners' nibbles empty.
.GLOBAL _tk_pool_head_owner;
_tk_pool_head_owner:
    R0 = DM(_tk_pool_mask);
    R2 = -R1;
    R2 = LSHIFT R0 BY R2;
    BTST R2 BY 0;
    IF SZ JUMP _tk_pool_lead;
    R2 = FEXT R1 BY 0:3;
    R2 = LSHIFT R2 BY 2;
    R2 = -R2;
    R3 = DM(_tk_pool_owners_low);
    R6 = DM(_tk_pool_owners_high);
    BTST R1 BY 3;
    IF NOT SZ R3 = R6;
    R3 = LSHIFT R3 BY R2;
    R0 = FEXT R3 BY 0:4;
    JUMP _tk_pool_check_marker;
_tk_pool_lead:
    // AUTO marks the owner even before it has borrowed a helper.
    R0 = R1;
_tk_pool_check_marker:
    R2 = FEXT R0 BY 0:3;
    R2 = LSHIFT R2 BY 2;
    R2 = -R2;
    R3 = DM(_tk_pool_owners_low);
    R6 = DM(_tk_pool_owners_high);
    BTST R0 BY 3;
    IF NOT SZ R3 = R6;
    R3 = LSHIFT R3 BY R2;
    R3 = FEXT R3 BY 0:4;
    R1 = TK_POOL_RELAY_OWNER;
    COMP(R3,R1);
    IF NE JUMP _tk_pool_no_head;
    RTS;
_tk_pool_no_head:
    R0 = -1;
    RTS;

// Call once per block, after the native track processors and before any
// custom capture or native send reads a track bus. Leave owners untouched
// in the disabled case. The physical helpers have their own note, filter,
// amp and pan state already applied to these planar stereo buses.
// Every block also inspects two tracks for the quiet mask (below), so no
// block carries the scan of all sixteen.
.GLOBAL _tk_pool_fold;
_tk_pool_fold:
    DM(_tk_pool_r0) = R0;
    R0 = MODE1;
    DM(_tk_pool_mode) = R0;
    BIT CLR MODE1 0x200000;
    NOP;
    NOP;
    R0 = ASTATX;
    DM(_tk_pool_astat) = R0;
    R0 = DM(_tk_pool_quiet_phase);
    R0 = R0 + 1;
    R0 = FEXT R0 BY 0:3;
    DM(_tk_pool_quiet_phase) = R0;
    DM(_tk_pool_r1) = R1;
    DM(_tk_pool_r2) = R2;
    DM(_tk_pool_r3) = R3;
    DM(_tk_pool_r4) = R4;
    DM(_tk_pool_r5) = R5;
    DM(_tk_pool_r6) = R6;
    DM(_tk_pool_r7) = R7;
    R7 = STKYX;
    DM(_tk_pool_stky) = R7;
    R7 = I0;
    DM(_tk_pool_i0) = R7;
    R7 = I1;
    DM(_tk_pool_i1) = R7;
    R7 = M5;
    DM(_tk_pool_m5) = R7;
    R7 = M6;
    DM(_tk_pool_m6) = R7;
    R7 = L0;
    DM(_tk_pool_l0) = R7;
    R7 = L1;
    DM(_tk_pool_l1) = R7;
    L0 = 0;
    L1 = 0;
    M5 = 0;
    M6 = 1;
    CALL _tk_pool_quiet_scan;
    R0 = DM(_tk_pool_mask);
    R0 = PASS R0;
    IF EQ JUMP _tk_pool_restore;
    R2 = 0;
    R3 = DM(_tk_pool_owners_high);
    R4 = DM(_tk_pool_owners_low);
    R5 = PASS R0;
    R6 = TK_TRACK_BUS;
_tk_pool_bus:
    BTST R5 BY 0;
    IF SZ JUMP _tk_pool_next_bus;
    R7 = FEXT R4 BY 0:4;
    R7 = LSHIFT R7 BY 8;
    R0 = TK_TRACK_BUS;
    R7 = R7 + R0;
    I0 = R6;
    I1 = R7;
    LCNTR = 64, DO _tk_pool_sample UNTIL LCE;
        R0 = DM(I0,M5);
        R1 = DM(I1,M5);
        F1 = F1 + F0;
        DM(I1,M6) = R1;
_tk_pool_sample: DM(I0,M6) = R2;
_tk_pool_next_bus:
    R5 = LSHIFT R5 BY -1;
    R5 = PASS R5;
    IF EQ JUMP _tk_pool_restore;
    R7 = 0x100;
    R6 = R6 + R7;
    R7 = LSHIFT R3 BY 28;
    R4 = LSHIFT R4 BY -4;
    R4 = R4 OR R7;
    R3 = LSHIFT R3 BY -4;
    JUMP _tk_pool_bus;
_tk_pool_restore:
    R7 = DM(_tk_pool_i0);
    I0 = R7;
    R7 = DM(_tk_pool_i1);
    I1 = R7;
    R7 = DM(_tk_pool_l0);
    L0 = R7;
    R7 = DM(_tk_pool_l1);
    L1 = R7;
    R7 = DM(_tk_pool_m5);
    M5 = R7;
    R7 = DM(_tk_pool_m6);
    M6 = R7;
    R7 = DM(_tk_pool_stky);
    STKYX = R7;
    R7 = DM(_tk_pool_r7);
    R6 = DM(_tk_pool_r6);
    R5 = DM(_tk_pool_r5);
    R4 = DM(_tk_pool_r4);
    R3 = DM(_tk_pool_r3);
    R2 = DM(_tk_pool_r2);
    R1 = DM(_tk_pool_r1);
    R0 = DM(_tk_pool_astat);
    ASTATX = R0;
    R0 = DM(_tk_pool_mode);
    MODE1 = R0;
    NOP;
    NOP;
    R0 = DM(_tk_pool_r0);
    RTS;

// Inspect a native stereo cursor pair and its completed track block.
// AHD/ADSR state may park in release after a source ends. Cursor inactivity
// plus output below a half 24-bit sample step excludes the audible tail.
// Native filters can keep decaying for seconds below this level.
// A looped cursor stays claimed even if its envelope has reached silence.
// The phase counts the blocks of a round of eight. The block of phase p
// inspects tracks 2p and 2p + 1, each over that whole block, and the block
// of phase 7 publishes the mask of all sixteen with a new serial: a mask
// every eight blocks, as when one block inspected them all, with each
// track's block up to seven blocks older than the mask. A round that did
// not start at phase 0 leaves the tracks it missed marked as not quiet.
_tk_pool_quiet_scan:
    R0 = DM(_tk_pool_quiet_phase);
    R7 = DM(_tk_pool_quiet_pending);
    R0 = PASS R0;
    IF EQ R7 = R7 - R7;
    // the pair's first native sample record + 0x1b8 (0x760 bytes a pair)
    R1 = LSHIFT R0 BY 11;
    R2 = LSHIFT R0 BY 7;
    R1 = R1 - R2;
    R2 = LSHIFT R0 BY 5;
    R1 = R1 - R2;
    R2 = 0x241484;
    R1 = R1 + R2;
    I1 = R1;
    R6 = LSHIFT R0 BY 9;
    R2 = TK_TRACK_BUS;
    R6 = R6 + R2;
    R2 = 2;
    R5 = 0x00010001; // ACTIVE and seed-pending byte flags
_tk_pool_quiet_track:
    R7 = LSHIFT R7 BY -1;
    R0 = DM(0,I1);
    R1 = DM(0x76,I1);
    R0 = R0 OR R1;
    R0 = R0 AND R5;
    IF NE JUMP _tk_pool_quiet_next;
    I0 = R6;
    R4 = 0;
    // Pipeline the next load, stopping before the end of this track's bus.
    R0 = DM(I0,M6);
    LCNTR = 63, DO _tk_pool_quiet_sample UNTIL LCE;
        R0 = FEXT R0 BY 0:31;
_tk_pool_quiet_sample: R4 = MAX(R4,R0), R0 = DM(I0,M6);
    R0 = FEXT R0 BY 0:31;
    R4 = MAX(R4,R0);
    // Nonnegative float words sort by magnitude. Infinity and NaN are above the threshold.
    R3 = 0x33800000; // 2^-24
    COMP(R4,R3);
    IF LT R7 = BSET R7 BY 15;
_tk_pool_quiet_next:
    MODIFY(I1,0xec) (NW);
    R3 = 0x100;
    R6 = R6 + R3;
    R2 = R2 - 1;
    IF NE JUMP _tk_pool_quiet_track;
    DM(_tk_pool_quiet_pending) = R7;
    R0 = DM(_tk_pool_quiet_phase);
    R1 = 7;
    COMP(R0,R1);
    IF NE RTS;
    DM(_tk_pool_quiet_mask) = R7;
    R0 = DM(_tk_pool_quiet_serial);
    R0 = R0 + 1;
    R0 = FEXT R0 BY 0:16;
    R0 = PASS R0;
    IF EQ R0 = R0 + 1;
    DM(_tk_pool_quiet_serial) = R0;
    RTS;

// DSP-link calls this only when its own transfer ID is zero. I4 is the
// outgoing reply. The idle link ACK cell holds the low-halfword-first packet.
.GLOBAL _tk_pool_quiet_reply;
_tk_pool_quiet_reply:
    R0 = DM(_tk_pool_quiet_serial);
    R0 = PASS R0;
    IF EQ RTS;
    R3 = DM(_tk_pool_quiet_mask);
    R6 = DM(_tk_pool_tag);
    R5 = 0x32f7;
    R5 = R5 XOR R3;
    R5 = R5 XOR R0;
    R5 = R5 XOR R6;
    R5 = LSHIFT R5 BY 16;
    R5 = R5 OR R0;
    R3 = LSHIFT R3 BY 16;
    R0 = 0x4b51;
    R6 = R6 XOR R0;
    R3 = R3 OR R6;
    R0 = I4;
    I2 = R0;
    DM(24,I2) = R3;
    DM(25,I2) = R5;
    RTS;

.SECTION/DM seg_dmda;
.VAR _tk_pool_mask;
.VAR _tk_pool_owners_low;
.VAR _tk_pool_owners_high;
.VAR _tk_pool_tag;
.VAR _tk_pool_rx_r6;
.VAR _tk_pool_r0[2];
.VAR _tk_pool_mode[2];
.VAR _tk_pool_quiet_phase;
.VAR _tk_pool_quiet_serial;
.VAR _tk_pool_quiet_mask;
.VAR _tk_pool_quiet_pending;
.VAR _tk_pool_astat;
.VAR _tk_pool_stky;
.VAR _tk_pool_r1;
.VAR _tk_pool_r2;
.VAR _tk_pool_r3;
.VAR _tk_pool_r4;
.VAR _tk_pool_r5;
.VAR _tk_pool_r6;
.VAR _tk_pool_r7;
.VAR _tk_pool_i0;
.VAR _tk_pool_i1;
.VAR _tk_pool_l0;
.VAR _tk_pool_l1;
.VAR _tk_pool_m5;
.VAR _tk_pool_m6;
