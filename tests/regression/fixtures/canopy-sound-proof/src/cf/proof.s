/* DTII 1.17 emulator-only trampolines. These must never ship to hardware. */
    .text
    .even
    .globl canopy_proof_load_entry
canopy_proof_load_entry:
    lea 12(%sp),%sp
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr canopy_proof_loaded
    addq.l #8,%sp
    moveq #1,%d0
    jmp 0x400db802

/* Only test/canopy-load-preflight patches the card wrapper's call site.
 * Its arguments are already on the stack. Replay the stock call, or return
 * zero to the wrapper before it calls the project loader at all. */
    .globl canopy_proof_card_load_guard
canopy_proof_card_load_guard:
    jsr canopy_proof_load_begin
    tst.l %d0
    bne.s 1f
    jsr 0x40046906
    move.l %d0,-(%sp)
    jsr canopy_proof_load_end
    move.l (%sp)+,%d0
    bra.s 2f
1:
    moveq #0,%d0
2:
    move.l %d2,-(%sp)
    jmp 0x40046c68

    .globl canopy_proof_save_entry
canopy_proof_save_entry:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l 28(%sp),-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w canopy_proof_save_original
    lea 12(%sp),%sp
    move.l %d0,%d3
    move.l %d3,-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr canopy_proof_saved
    lea 12(%sp),%sp
    move.l %d3,%d0
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
canopy_proof_save_original:
    lea -36(%sp),%sp
    movem.l %d2-%d5/%a2-%a6,(%sp)
    jmp 0x400dba9c

/* Only selected by test/canopy-copy-handoff. Preserve all caller-saved
 * registers and CCR before inspecting arguments: stock memcpy's first two
 * MOVEA instructions do not change CCR. An observed UI copy enters from
 * 0x401aaccc with length 0x450. Pattern copy was observed at 0x40047a16
 * with length 0x5574. This global hook is NOT reentrant-safe and must never
 * be used outside the isolated emulator test. */
    .globl canopy_proof_copy_entry
canopy_proof_copy_entry:
    lea -20(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.w %sr,%d0
    move.w %d0,18(%sp)
    move.l 20(%sp),%d0
    cmpi.l #0x401aaccc,%d0
    beq.s 2f
    cmpi.l #0x40047a16,%d0
    bne.s 1f
    move.l 32(%sp),%d0
    cmpi.l #0x5574,%d0
    bne.s 1f
    move.l 28(%sp),-(%sp)
    jsr canopy_proof_copy_kit
    addq.l #4,%sp
    bra.s 1f
2:
    move.l 32(%sp),%d0
    cmpi.l #0x450,%d0
    bne.s 1f
    move.l 28(%sp),-(%sp)
    jsr canopy_proof_copy_source
    addq.l #4,%sp
1:
    move.w 18(%sp),%d0
    move.w %d0,%sr
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 20(%sp),%sp
    movea.l 4(%sp),%a1
    movea.l 8(%sp),%a0
    jmp 0x40136390
