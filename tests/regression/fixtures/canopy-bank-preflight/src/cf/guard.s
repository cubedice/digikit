/* Disposable emulator-only wrapper for DTII 1.17 bank loader. */
    .text
    .even
    .globl canopy_bank_loaded_entry
canopy_bank_loaded_entry:
    lea 12(%sp),%sp
    jsr canopy_bank_loaded
    moveq #1,%d0
    jmp 0x400db802

    .globl canopy_bank_guard
canopy_bank_guard:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l %a2,-(%sp)
    jsr canopy_bank_is_target
    addq.l #4,%sp
    move.l %d0,%d2
    tst.l %d2
    beq.s 1f
    jsr canopy_bank_begin
    tst.l %d0
    bne.s 2f
1:
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w canopy_bank_original
    addq.l #8,%sp
    move.l %d0,%d3
    tst.l %d2
    beq.s 3f
    jsr canopy_bank_end
3:
    move.l %d3,%d0
    bra.s 4f
2:
    moveq #0,%d0
4:
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
canopy_bank_original:
    lea -20(%sp),%sp
    movem.l %d2-%d4/%a2-%a3,(%sp)
    jmp 0x400db838
