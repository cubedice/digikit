/* Emulator-only DTII 1.17 memcpy observation point; never ship on hardware.
 *
 * Incoming ABI is stock memcpy(dst, src, length), with a return PC at (sp).
 * The original entry's first two MOVEA instructions are replayed exactly.
 * D0 is caller-saved and is overwritten by the original instruction at the
 * continuation; CMP leaves X unchanged and the continuation's MOVEQ sets
 * N/Z/V/C before any original conditional branch. No other state is changed.
 *
 * The exported match label is a non-mutating observation point for external
 * instruction tracing. No shared counters, locks, allocation, or callbacks
 * are used, so interrupt/reentrant calls cannot corrupt probe state. This
 * tests only copies of exactly one full Sound; bulk kit/project copies and
 * inlined copies need separate observation. */
    .text
    .even
    .globl canopy_copy_probe_entry
canopy_copy_probe_entry:
    move.l 12(%sp),%d0
    cmpi.l #0x450,%d0
    bne.s 1f
    .globl canopy_copy_probe_match
canopy_copy_probe_match:
    nop
1:
    movea.l 4(%sp),%a1
    movea.l 8(%sp),%a0
    jmp 0x40136390
