/* DT2 1.17 stock memcpy, memset and bzero hooks (core/copy-watch, docs/plans/fork-image-diet.md section 9).
 *
 * The stock path is unchanged for lengths below one Sound (0x450). A longer copy or fill calls C (copy-watch.c) around a call of the
 * stock body when a client watches every long one (cw_long_watch: CANOPY's Sound association store exists) or when its length is a
 * pattern's (0x1db8c) or, for memcpy, a track's (0x6a5), which the track keyboard follows. The C ABI is kept: d0-d1/a0-a1 and CCR
 * are scratch, memcpy and memset return the destination. Arguments are pushed again for every call because a C callee owns its
 * argument slots. */
    .text
    .even

    .globl cw_memcpy_entry
cw_memcpy_entry:
    move.l 12(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l cw_long_watch
    bne.s 3f
    cmpi.l #0x1db8c,%d0
    beq.s 3f
    cmpi.l #0x6a5,%d0
    bne.s 1f
3:
    lea -12(%sp),%sp
    movem.l %d2-%d4,(%sp)
    move.l 16(%sp),%d2
    move.l 20(%sp),%d3
    move.l 24(%sp),%d4
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr cw_copy_before
    lea 12(%sp),%sp
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    bsr.s 2f
    lea 12(%sp),%sp
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr cw_copy_after
    lea 12(%sp),%sp
    move.l %d2,%d0
    movem.l (%sp),%d2-%d4
    lea 12(%sp),%sp
    rts
1:
2:
    movea.l 4(%sp),%a1
    movea.l 8(%sp),%a0
    jmp cw_memcpy_resume

    .globl cw_memset_entry
cw_memset_entry:
    move.l 12(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l cw_long_watch
    bne.s 3f
    cmpi.l #0x1db8c,%d0
    bne.s 1f
3:
    lea -12(%sp),%sp
    movem.l %d2-%d4,(%sp)
    move.l 16(%sp),%d2
    move.l 20(%sp),%d3
    move.l 24(%sp),%d4
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    bsr.s 2f
    lea 12(%sp),%sp
    move.l %d4,-(%sp)
    move.l %d2,-(%sp)
    jsr cw_cleared
    addq.l #8,%sp
    move.l %d2,%d0
    movem.l (%sp),%d2-%d4
    lea 12(%sp),%sp
    rts
1:
2:
    movea.l 8(%sp),%a0
    mvz.b 11(%sp),%d0
    jmp cw_memset_resume

    .globl cw_bzero_entry
cw_bzero_entry:
    move.l 8(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l cw_long_watch
    bne.s 3f
    cmpi.l #0x1db8c,%d0
    bne.s 1f
3:
    lea -8(%sp),%sp
    movem.l %d2-%d3,(%sp)
    move.l 12(%sp),%d2
    move.l 16(%sp),%d3
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    bsr.s 2f
    addq.l #8,%sp
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr cw_cleared
    addq.l #8,%sp
    movem.l (%sp),%d2-%d3
    lea 8(%sp),%sp
    rts
1:
2:
    movea.l 4(%sp),%a0
    move.l 8(%sp),%d0
    jmp cw_bzero_resume

