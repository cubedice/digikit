// DT2 1.17 only. This descriptor exists only when the six allocator writes
// from this module were applied to the hash-verified stock image.
// CF offsets start at 0x400; LP0 sends (offset - 0x400) >> 12.
// DSP bytes = 0x8422b9c8 + CF sample offset - 0x400.
// Reserved [0x9ca2b9c8, 0x9d22b9c8), immediately below stock preview.
// The descriptor owns one shared buffer, not an allocator for several clients.
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
