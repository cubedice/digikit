// DT2 1.17 only. This descriptor exists only when the six allocator writes
// from this module were applied to the hash-verified stock image.
// CF offsets start at 0x400; LP0 sends (offset - 0x400) >> 12.
// DSP bytes = 0x8422b9c8 + CF sample offset - 0x400.
// Reserved [0x9ba2b9c8, 0x9d22b9c8), immediately below stock preview.
// The existing loop buffer keeps its original address and 8 MiB length.
// Two independent phrase banks occupy the preceding 16 MiB. Each descriptor
// has one owner; neither is an allocator for several clients.
// Capture must overwrite every readable frame before publishing valid audio.

// The object builder requires a PM section; this provider has no entry point.
.SECTION/PM seg_pmco;
    NOP;

.SECTION/DM seg_dmda;
.GLOBAL _loop_buffer_info;
.VAR _loop_buffer_info = 0x4c425546;  // "LBUF"
.VAR _loop_buffer_version = 1;
.VAR _loop_buffer_base = 0x9ca2b9c8;
.VAR _loop_buffer_bytes = 0x00800000;

// Phrase banks contain planar float audio and one Q16 master-phase stamp per
// 32-frame block. 1,032,192 frames use 8,386,560 bytes including their stamps,
// leaving 2048 bytes of guard space at the end of each 8 MiB bank.
.GLOBAL _phrase_storage_info;
.VAR _phrase_storage_info = 0x50484231; // PH_STORAGE_MAGIC, "PHB1"
.VAR _phrase_storage_version = 1;
.VAR _phrase_storage_bank0 = 0x9ba2b9c8;
.VAR _phrase_storage_bank1 = 0x9c22b9c8;
.VAR _phrase_storage_bank_bytes = 0x00800000;
.VAR _phrase_storage_frames = 1032192;

// The two mailboxes between the loop effect (Overloop) and the phrase effect (PRISM), here because both effects require this
// provider and either may be in an image without the other (sdk/dsp/contract/dk_loop.inc). _loop_tap (68 words, LT_*): the
// loop times the crossfader, which the phrase effect hears with INPT at LOOP; the loop effect writes it. _loop_wet (68
// words, LW_*): the phrase effect's output, which the loop effect adds behind its crossfader while it runs; each word has
// one writer. Both start as zero: no magic, and LW_TAKEN does not move without a loop effect.
.PKDATA _loop_tap, 272;
.PKDATA _loop_wet, 272;
