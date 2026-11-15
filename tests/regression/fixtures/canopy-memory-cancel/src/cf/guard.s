/* DISPOSABLE EMULATOR ONLY. Entered in the stock command dispatcher with
 * the two project-loader arguments on the stack. Not a production guard. */
    .text
    .even
    .globl canopy_memory_cancel_guard
canopy_memory_cancel_guard:
    jsr canopy_proof_load_begin
    tst.l %d0
    bne.s 1f
    jsr 0x40043f74
    move.l %d0,-(%sp)
    jsr canopy_proof_load_end
    move.l (%sp)+,%d0
    jmp 0x400d5238
1:
    addq.l #8,%sp
    jsr 0x401af782
    move.l %d0,-(%sp)
    jsr 0x40135a38
    addq.l #4,%sp
    moveq #1,%d0
    move.l %d0,canopy_memory_cleanup_armed
    jmp 0x400d5384

    .globl canopy_memory_cleanup_probe
canopy_memory_cleanup_probe:
    tst.l canopy_memory_cleanup_armed
    beq.s 2f
    movea.l %d2,%a0
    move.l (%a0),canopy_memory_cleanup_ptr
    addq.l #1,canopy_memory_cleanup_count
    clr.l canopy_memory_cleanup_armed
2:
    move.l %d2,-(%sp)
    jmp 0x400d5804
