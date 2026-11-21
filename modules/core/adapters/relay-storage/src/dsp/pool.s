// One packed stereo word per frame. First 64 KiB of the 32 MiB lease contains
// all metadata. Free pages and pages of each take are linked via NEXT; a
// per-take directory also gives constant-time seeks into scattered pages.
#include "dk_relay.inc"
#define FREE 0
#define FRESH 1
#define RECYCLED 2
#define LIMIT 3
#define OFFER 4
#define ROOTS 8
#define PIN_ROOT 15
#define WRITER_ROOT 16
#define NEXT_WORDS 128
#define RECORDS_BYTES 4096
// At most 73 live roots. Index every eighth page, then follow at most seven
// NEXT links. All descriptors and directories fit within the first 64 KiB.
#define RECORD_STRIDE 320
#define REFS 0
#define FRAMES 1
#define PAGES 2
#define FIRST 3
#define LAST 4
#define PARENT 5
#define UNSEEDED 6
#define SWEEP 7
#define DIRECTORY 8
// Two validity words per physical page, in the unused upper metadata area.
#define VALID_BYTES 32768

.SECTION/PM seg_pmco;
.EXTERN _relay_storage_info;
.GLOBAL _relay_pool;
_relay_pool:
    I4 = _relay_storage_info;
    R15 = DM(2,I4);
    I5 = R15;
    R0 = RLY_RESET;
    COMP(R4,R0);
    IF EQ JUMP _rp_reset;
    I3 = _relay_ready;
    R0 = DM(0,I3);
    R0 = PASS R0;
    IF EQ JUMP _rp_fail;
    R0 = RLY_AVAILABLE;
    COMP(R4,R0);
    IF EQ JUMP _rp_available;
    R0 = RLY_BEGIN;
    COMP(R4,R0);
    IF EQ JUMP _rp_begin;
    R0 = RLY_RETARGET;
    COMP(R4,R0);
    IF EQ JUMP _rp_retarget;
    R0 = RLY_ACQUIRE;
    COMP(R4,R0);
    IF EQ JUMP _rp_append;
    R0 = RLY_PUBLISH;
    COMP(R4,R0);
    IF EQ JUMP _rp_publish;
    R0 = RLY_APPEND;
    COMP(R4,R0);
    IF EQ JUMP _rp_append;
    R0 = RLY_ABORT;
    COMP(R4,R0);
    IF EQ JUMP _rp_abort;
    R0 = RLY_UNPIN;
    COMP(R4,R0);
    IF EQ JUMP _rp_unpin;
    R0 = RLY_REWRITE;
    COMP(R4,R0);
    IF EQ JUMP _rp_rewrite;
    R0 = RLY_TAPE; COMP(R4,R0); IF EQ JUMP _rp_tape;
    // All remaining commands take a slot number in R8.
    R0 = RLY_PINNED;
    COMP(R8,R0);
    IF LT JUMP _rp_fail;
    R0 = RLY_SLOT_COUNT;
    COMP(R8,R0);
    IF GE JUMP _rp_fail;
    R0 = LSHIFT R8 BY 2;
    R0 = R15 + R0;
    I3 = R0;
    R10 = DM(ROOTS,I3);
    I2 = R10;
    R0 = RLY_TIDY; COMP(R4,R0); IF EQ JUMP _rp_tidy;
    R0 = RLY_READ_SPAN;
    COMP(R4,R0);
    IF EQ JUMP _rp_read;
    R0 = RLY_LENGTH;
    COMP(R4,R0);
    IF EQ JUMP _rp_length;
    R0 = RLY_PINNED;
    COMP(R8,R0);
    IF EQ JUMP _rp_fail;
    R0 = RLY_WRITER;
    COMP(R8,R0);
    IF EQ JUMP _rp_fail;
    R0 = RLY_FINISH_DUB;
    COMP(R4,R0);
    IF EQ JUMP _rp_finish_dub;
    R0 = RLY_COMMIT;
    COMP(R4,R0);
    IF EQ JUMP _rp_commit;
    R0 = RLY_CLEAR;
    COMP(R4,R0);
    IF EQ JUMP _rp_clear;
    R0 = RLY_SHARE; COMP(R4,R0); IF EQ JUMP _rp_share;
    R0 = RLY_SWAP;
    COMP(R4,R0);
    IF EQ JUMP _rp_swap;
    R0 = RLY_PIN;
    COMP(R4,R0);
    IF EQ JUMP _rp_pin;
_rp_fail:
    R0 = -1;
    RTS;
_rp_ok:
    R0 = 0;
    RTS;

_rp_reset:
    // Explicit activation only after readers have quiesced. No PCM clear.
    R0 = RLY_PAGE_COUNT;
    DM(FREE,I5) = R0;
    R0 = 1;
    DM(FRESH,I5) = R0;
    R0 = 0;
    DM(RECYCLED,I5) = R0;
    R1 = RLY_SLOT_COUNT;
    R2 = RECORDS_BYTES;
    R2 = R15 + R2;
    R3 = R15;
_rp_reset_next:
    I2 = R2;
    DM(REFS,I2) = R0;
    I3 = R3;
    DM(ROOTS,I3) = R0;
    R6 = RECORD_STRIDE;
    R2 = R2 + R6;
    R6 = 4;
    R3 = R3 + R6;
    R1 = R1 - 1;
    IF NE JUMP _rp_reset_next;
    I3 = _relay_ready;
    R1 = 1;
    DM(0,I3) = R1;
    RTS;

_rp_available:
    R0 = DM(FREE,I5);
    R0 = LSHIFT R0 BY 14;
    R10 = DM(WRITER_ROOT,I5);
    R10 = PASS R10;
    IF EQ RTS;
    I2 = R10;
    R1 = DM(PAGES,I2);
    R1 = LSHIFT R1 BY 14;
    R2 = DM(FRAMES,I2);
    R1 = R1 - R2;
    R0 = R0 + R1;
    RTS;

_rp_begin:
    R10 = DM(WRITER_ROOT,I5);
    R10 = PASS R10;
    IF NE JUMP _rp_fail;
    R8 = PASS R8;
    IF LT JUMP _rp_fail;
    R0 = DM(FREE,I5);
    R0 = LSHIFT R0 BY 14;
    R0 = PASS R0;
    IF EQ JUMP _rp_fail;
    COMP(R8,R0);
    IF GT JUMP _rp_fail;
    // Each new writer needs one free descriptor.
    R1 = RECORDS_BYTES;
    R1 = R15 + R1;
    R3 = RLY_SLOT_COUNT;
_rp_find_record:
    I2 = R1;
    R2 = DM(REFS,I2);
    R2 = PASS R2;
    IF EQ JUMP _rp_begin_found;
    R2 = RECORD_STRIDE;
    R1 = R1 + R2;
    R3 = R3 - 1;
    IF NE JUMP _rp_find_record;
    JUMP _rp_fail;
_rp_begin_found:
    R0 = 1;
    DM(REFS,I2) = R0;
    R0 = 0;
    DM(FRAMES,I2) = R0;
    DM(PAGES,I2) = R0;
    DM(PARENT,I2) = R0; DM(UNSEEDED,I2) = R0; DM(SWEEP,I2) = R0;
    DM(LAST,I2) = R0;
    DM(OFFER,I5) = R0;
    DM(LIMIT,I5) = R8;
    DM(WRITER_ROOT,I5) = R1;
    RTS;
_rp_retarget:
    R0 = DM(OFFER,I5);
    R0 = PASS R0;
    IF NE JUMP _rp_fail;
    R10 = DM(WRITER_ROOT,I5);
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    I2 = R10;
    R0 = DM(FRAMES,I2);
    R0 = PASS R0;
    IF NE JUMP _rp_fail;
    R0 = DM(FREE,I5);
    R0 = LSHIFT R0 BY 14;
    COMP(R8,R0);
    IF GT JUMP _rp_fail;
    R8 = PASS R8;
    IF LT JUMP _rp_fail;
    DM(LIMIT,I5) = R8;
    JUMP _rp_ok;

_rp_append:
    R0 = DM(OFFER,I5);
    R0 = PASS R0;
    IF NE JUMP _rp_fail;
    R10 = DM(WRITER_ROOT,I5);
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    I2 = R10;
    R12 = DM(FRAMES,I2);
    R0 = DM(LIMIT,I5);
    R0 = PASS R0;
    IF EQ JUMP _rp_append_page;
    COMP(R12,R0);
    IF GE JUMP _rp_fail;
_rp_append_page:
    R0 = 16383;
    R1 = R12 AND R0;
    IF NE JUMP _rp_append_existing;
    R0 = DM(FREE,I5);
    R0 = PASS R0;
    IF EQ JUMP _rp_fail;
    R0 = R0 - 1;
    DM(FREE,I5) = R0;
    R6 = DM(RECYCLED,I5);
    R6 = PASS R6;
    IF NE JUMP _rp_reuse;
    R6 = DM(FRESH,I5);
    R0 = R6 + 1;
    DM(FRESH,I5) = R0;
    JUMP _rp_link_page;
_rp_reuse:
    R0 = LSHIFT R6 BY 2;
    R0 = R15 + R0;
    I3 = R0;
    R0 = DM(NEXT_WORDS,I3);
    DM(RECYCLED,I5) = R0;
_rp_link_page:
    R0 = LSHIFT R6 BY 3; R1 = VALID_BYTES; R0 = R0 + R1; R0 = R0 + R15; I3 = R0;
    R0 = 0; DM(0,I3) = R0; DM(1,I3) = R0;
    R0 = DM(PAGES,I2);
    R1 = 7;
    R1 = R0 AND R1;
    IF NE JUMP _rp_link_previous;
    R1 = LSHIFT R0 BY -3;
    R1 = LSHIFT R1 BY 2;
    R1 = R10 + R1;
    I3 = R1;
    DM(DIRECTORY,I3) = R6;
_rp_link_previous:
    R0 = R0 + 1;
    DM(PAGES,I2) = R0;
    R1 = DM(LAST,I2);
    R1 = PASS R1;
    IF EQ JUMP _rp_first;
    R1 = LSHIFT R1 BY 2;
    R1 = R15 + R1;
    I3 = R1;
    DM(NEXT_WORDS,I3) = R6;
    JUMP _rp_set_last;
_rp_first:
    DM(FIRST,I2) = R6;
_rp_set_last:
    DM(LAST,I2) = R6;
_rp_append_existing:
    R0 = DM(LAST,I2);
    R0 = LSHIFT R0 BY 16;
    R0 = R15 + R0;
    R1 = 16383;
    R1 = R12 AND R1;
    R3 = RLY_ACQUIRE;
    COMP(R4,R3);
    IF EQ JUMP _rp_offer;
    R1 = LSHIFT R1 BY 2;
    R0 = R0 + R1;
    I3 = R0;
    DM(0,I3) = R8;
    R0 = R12 + 1;
    DM(FRAMES,I2) = R0;
    RTS;

_rp_offer:
    R6 = LSHIFT R1 BY 2;
    R0 = R0 + R6;
    R6 = RLY_FRAMES_PER_PAGE;
    R1 = R6 - R1;
    R6 = DM(LIMIT,I5);
    R6 = PASS R6;
    IF EQ JUMP _rp_offer_ready;
    R6 = R6 - R12;
    R1 = MIN(R1,R6);
_rp_offer_ready:
    DM(OFFER,I5) = R1;
    RTS;

_rp_publish:
    R10 = DM(WRITER_ROOT,I5);
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    R8 = PASS R8;
    IF LE JUMP _rp_fail;
    R0 = DM(OFFER,I5);
    COMP(R8,R0);
    IF GT JUMP _rp_fail;
    I2 = R10;
    R0 = DM(FRAMES,I2);
    R0 = R0 + R8;
    DM(FRAMES,I2) = R0;
    R1 = 0;
    DM(OFFER,I5) = R1;
    RTS;
_rp_length:
    R0 = PASS R10;
    IF EQ RTS;
    R0 = DM(FRAMES,I2);
    RTS;
_rp_tidy:
    R10 = PASS R10; IF EQ JUMP _rp_ok;
    R0 = DM(PARENT,I2); R0 = PASS R0; IF EQ JUMP _rp_ok;
    R12 = DM(SWEEP,I2); R2 = DM(FRAMES,I2); COMP(R12,R2); IF GE JUMP _rp_ok;
    R1 = 256; R1 = R12 + R1; DM(SWEEP,I2) = R1;
    JUMP _rp_read;
_rp_rewrite:
    R10 = DM(WRITER_ROOT,I5);
    I2 = R10;
_rp_read:
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    R12 = PASS R12;
    IF LT JUMP _rp_fail;
    R2 = DM(FRAMES,I2);
    COMP(R12,R2);
    IF GE JUMP _rp_fail;
    CALL _rp_address;
    R6 = DM(UNSEEDED,I2); R6 = PASS R6; IF NE CALL _rp_seed;
    JUMP _rp_span_result;
_rp_address:
    R3 = LSHIFT R12 BY -17;
    R3 = LSHIFT R3 BY 2;
    R3 = R10 + R3;
    I3 = R3;
    R0 = DM(DIRECTORY,I3);
    R1 = LSHIFT R12 BY -14;
    R3 = 7;
    R1 = R1 AND R3;
    IF EQ JUMP _rp_page_found;
_rp_follow:
    R3 = LSHIFT R0 BY 2;
    R3 = R3 + R15;
    I3 = R3;
    R0 = DM(NEXT_WORDS,I3);
    R1 = R1 - 1;
    IF NE JUMP _rp_follow;
_rp_page_found:
    R7 = PASS R0;
    R0 = LSHIFT R0 BY 16;
    R0 = R15 + R0;
    R1 = 16383;
    R3 = R12 AND R1;
    R6 = LSHIFT R3 BY 2;
    R0 = R0 + R6;
    RTS;
_rp_span_result:
    R6 = RLY_REWRITE;
    COMP(R4,R6);
    IF EQ JUMP _rp_patch_frame;
    R6 = RLY_FRAMES_PER_PAGE;
    R1 = R6 - R3;
    R2 = R2 - R12;
    R1 = MIN(R1,R2);
    R6 = DM(UNSEEDED,I2); R6 = PASS R6; IF EQ RTS;
    R6 = 255; R6 = R12 AND R6; R7 = 256; R6 = R7 - R6; R1 = MIN(R1,R6);
    RTS;
_rp_patch_frame:
    I3 = R0;
    DM(0,I3) = R8;
    JUMP _rp_ok;

// Reserve page descriptors only. The first access to a 256-frame group
// materializes its old contents, keeping the initial punch independent of tape duration.
_rp_tape:
    R10 = DM(WRITER_ROOT,I5); R10 = PASS R10; IF EQ JUMP _rp_fail;
    I2 = R10; R0 = DM(FRAMES,I2); R0 = PASS R0; IF NE JUMP _rp_fail;
    R0 = DM(LIMIT,I5); R0 = PASS R0; IF LE JUMP _rp_fail;
    R1 = -1; COMP(R8,R1); IF EQ JUMP _rp_tape_blank;
    R1 = RLY_TRACK_MAIN; COMP(R8,R1); IF LT JUMP _rp_fail;
    R1 = RLY_SLOT_COUNT; COMP(R8,R1); IF GE JUMP _rp_fail;
    R1 = LSHIFT R8 BY 2; R1 = R15 + R1; I3 = R1; R1 = DM(ROOTS,I3);
    R1 = PASS R1; IF EQ JUMP _rp_fail;
    I3 = R1; R2 = DM(FRAMES,I3); COMP(R2,R0); IF NE JUMP _rp_fail;
    R2 = DM(REFS,I3); R2 = R2 + 1; DM(REFS,I3) = R2; DM(PARENT,I2) = R1;
_rp_tape_blank:
    R1 = 255; R0 = R0 + R1; R0 = LSHIFT R0 BY -8; DM(UNSEEDED,I2) = R0;
_rp_tape_page:
    R4 = RLY_ACQUIRE; CALL _rp_append;
    R8 = R1; CALL _rp_publish;
    R1 = DM(LIMIT,I5); COMP(R0,R1); IF LT JUMP _rp_tape_page;
    JUMP _rp_ok;

// Return the group bitmap word and bit for physical page R7 and frame R12.
_rp_bit:
    R6 = LSHIFT R7 BY 3; R1 = VALID_BYTES; R6 = R6 + R1; R6 = R6 + R15;
    R1 = LSHIFT R12 BY -11; R7 = 4; R1 = R1 AND R7;
    R6 = R6 + R1; I3 = R6; R7 = LSHIFT R12 BY -8; R1 = 31; R7 = R7 AND R1;
    R1 = 1; R7 = LSHIFT R1 BY R7; R1 = DM(0,I3); R6 = R1 AND R7; RTS;
_rp_seed:
    CALL _rp_bit; R6 = PASS R6; IF NE RTS;
    R1 = R1 OR R7; DM(0,I3) = R1;
    DM(_rp_saved_frame) = R0; DM(_rp_saved_root) = R10;
    R1 = 255; R1 = R12 AND R1; R1 = LSHIFT R1 BY 2; R0 = R0 - R1; I0 = R0;
    R10 = DM(PARENT,I2);
_rp_ancestor:
    R10 = PASS R10; IF EQ JUMP _rp_zero_group;
    I2 = R10; CALL _rp_address;
    R6 = DM(UNSEEDED,I2); R6 = PASS R6; IF EQ JUMP _rp_copy_group;
    CALL _rp_bit; R6 = PASS R6; IF NE JUMP _rp_copy_group;
    R10 = DM(PARENT,I2); JUMP _rp_ancestor;
_rp_zero_group:
    R1 = 0; R6 = 256; LCNTR = R6, DO _rp_zero_word UNTIL LCE;
_rp_zero_word: DM(I0,M6) = R1;
    JUMP _rp_seeded;
_rp_copy_group:
    R1 = 255; R1 = R12 AND R1; R1 = LSHIFT R1 BY 2; R0 = R0 - R1; I1 = R0;
    R6 = 256; LCNTR = R6, DO _rp_copy_word UNTIL LCE;
        R1 = DM(I1,M6);
_rp_copy_word: DM(I0,M6) = R1;
_rp_seeded:
    R10 = DM(_rp_saved_root); I2 = R10;
    R6 = DM(UNSEEDED,I2); R6 = R6 - 1; DM(UNSEEDED,I2) = R6;
    IF NE JUMP _rp_seed_done;
    R10 = DM(PARENT,I2); DM(PARENT,I2) = R6; CALL _rp_release;
_rp_seed_done:
    R10 = DM(_rp_saved_root); I2 = R10; R0 = DM(_rp_saved_frame);
    R1 = 16383; R3 = R12 AND R1; R2 = DM(FRAMES,I2); RTS;

_rp_finish_dub:
    R0 = RLY_TRACK_T1;
    COMP(R8,R0);
    IF GE JUMP _rp_fail;
    R6 = 56; // ROOTS + (RLY_TRACK_UNDO - RLY_TRACK_MAIN)
    R6 = R6 + R8;
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    R1 = DM(FRAMES,I2);
    R0 = DM(WRITER_ROOT,I5);
    R0 = PASS R0;
    IF EQ JUMP _rp_fail;
    I2 = R0;
    R2 = DM(FRAMES,I2);
    COMP(R1,R2);
    IF NE JUMP _rp_fail;
    R1 = DM(OFFER,I5);
    R1 = PASS R1;
    IF NE JUMP _rp_fail;
    R6 = LSHIFT R6 BY 2;
    R6 = R15 + R6;
    I2 = R6;
    R2 = DM(0,I2);
    DM(0,I2) = R10;
    R10 = R2;
    JUMP _rp_publish_root;
_rp_commit:
    R0 = DM(OFFER,I5);
    R0 = PASS R0;
    IF NE JUMP _rp_fail;
    R0 = DM(WRITER_ROOT,I5);
    R0 = PASS R0;
    IF EQ JUMP _rp_fail;
    I2 = R0;
    R1 = DM(FRAMES,I2);
    R1 = PASS R1;
    IF EQ JUMP _rp_fail;
_rp_publish_root:
    DM(ROOTS,I3) = R0;
    R0 = 0;
    DM(WRITER_ROOT,I5) = R0;
    JUMP _rp_release;
_rp_share:
    R0 = RLY_TRACK_MAIN; COMP(R8,R0); IF LT JUMP _rp_fail;
    COMP(R12,R0); IF LT JUMP _rp_fail;
    R0 = RLY_TRACK_T1; COMP(R8,R0); IF GE JUMP _rp_fail;
    COMP(R12,R0); IF GE JUMP _rp_fail;
    R0 = LSHIFT R12 BY 2; R0 = R15 + R0; I2 = R0; R1 = DM(ROOTS,I2);
    R1 = PASS R1; IF EQ JUMP _rp_fail;
    I2 = R1; R0 = DM(REFS,I2); R0 = R0 + 1; DM(REFS,I2) = R0;
    DM(ROOTS,I3) = R1; JUMP _rp_release;
_rp_swap:
    R0 = RLY_TRACK_MAIN;
    COMP(R12,R0);
    IF LT JUMP _rp_fail;
    R0 = RLY_SLOT_COUNT;
    COMP(R12,R0);
    IF GE JUMP _rp_fail;
    R0 = LSHIFT R12 BY 2;
    R0 = R15 + R0;
    I2 = R0;
    R0 = DM(ROOTS,I2);
    DM(ROOTS,I2) = R10;
    DM(ROOTS,I3) = R0;
    JUMP _rp_ok;
_rp_pin:
    R0 = DM(PIN_ROOT,I5);
    R0 = PASS R0;
    IF NE JUMP _rp_fail;
    R10 = PASS R10;
    IF EQ JUMP _rp_fail;
    R0 = DM(REFS,I2);
    R0 = R0 + 1;
    DM(REFS,I2) = R0;
    DM(PIN_ROOT,I5) = R10;
    JUMP _rp_ok;
_rp_abort:
    R8 = RLY_WRITER;
    JUMP _rp_drop_slot;
_rp_unpin:
    R8 = RLY_PINNED;
_rp_drop_slot:
    R0 = LSHIFT R8 BY 2;
    R0 = R15 + R0;
    I3 = R0;
    R10 = DM(ROOTS,I3);
_rp_clear:
    R0 = 0;
    DM(ROOTS,I3) = R0;
_rp_release:
    R10 = PASS R10;
    IF EQ JUMP _rp_ok;
    I2 = R10;
    R0 = DM(REFS,I2);
    R0 = R0 - 1;
    DM(REFS,I2) = R0;
    IF NE JUMP _rp_ok;
    R1 = DM(PAGES,I2);
    R1 = PASS R1;
    IF EQ JUMP _rp_ok;
    R0 = DM(FREE,I5);
    R0 = R0 + R1;
    DM(FREE,I5) = R0;
    R0 = DM(LAST,I2);
    R0 = LSHIFT R0 BY 2;
    R0 = R15 + R0;
    I3 = R0;
    R0 = DM(RECYCLED,I5);
    DM(NEXT_WORDS,I3) = R0;
    R0 = DM(FIRST,I2);
    DM(RECYCLED,I5) = R0;
    R10 = DM(PARENT,I2); JUMP _rp_release;

.SECTION/DM seg_dmda;
.VAR _relay_ready = 0;
.VAR _rp_saved_root;
.VAR _rp_saved_frame;
