/* DT2 1.17 stock hooks of CANOPY Sound persistence (sound-persist.c).
 *
 * memcpy, memset and bzero take the stock path unchanged for lengths below
 * one Sound (0x450) and whenever the association store does not exist yet;
 * only longer copies and fills call C, around a call of the stock body. The
 * C ABI is kept: d0-d1/a0-a1 and CCR are scratch, memcpy and memset return
 * the destination. Arguments are pushed again for every call because a C
 * callee owns its argument slots. */
    .text
    .even

    .globl ip_memcpy_entry
ip_memcpy_entry:
    move.l 12(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l ip_sa_ready
    beq.s 1f
    lea -12(%sp),%sp
    movem.l %d2-%d4,(%sp)
    move.l 16(%sp),%d2
    move.l 20(%sp),%d3
    move.l 24(%sp),%d4
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr ip_sa_copy_before
    lea 12(%sp),%sp
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    bsr.s 2f
    lea 12(%sp),%sp
    move.l %d4,-(%sp)
    move.l %d3,-(%sp)
    move.l %d2,-(%sp)
    jsr ip_sa_copy_after
    lea 12(%sp),%sp
    move.l %d2,%d0
    movem.l (%sp),%d2-%d4
    lea 12(%sp),%sp
    rts
1:
2:
    movea.l 4(%sp),%a1
    movea.l 8(%sp),%a0
    jmp ip_memcpy_resume

    .globl ip_memset_entry
ip_memset_entry:
    move.l 12(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l ip_sa_ready
    beq.s 1f
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
    jsr ip_sa_cleared
    addq.l #8,%sp
    move.l %d2,%d0
    movem.l (%sp),%d2-%d4
    lea 12(%sp),%sp
    rts
1:
2:
    movea.l 8(%sp),%a0
    mvz.b 11(%sp),%d0
    jmp ip_memset_resume

    .globl ip_bzero_entry
ip_bzero_entry:
    move.l 8(%sp),%d0
    cmpi.l #0x450,%d0
    bcs.s 1f
    tst.l ip_sa_ready
    beq.s 1f
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
    jsr ip_sa_cleared
    addq.l #8,%sp
    movem.l (%sp),%d2-%d3
    lea 8(%sp),%sp
    rts
1:
2:
    movea.l 4(%sp),%a0
    move.l 8(%sp),%d0
    jmp ip_bzero_resume

/* Sound writer, C ABI (record, Sound, flag). The stock result is returned
 * unchanged; the extension is written into the record after a success. */
    .globl ip_save_entry
ip_save_entry:
    lea -16(%sp),%sp
    movem.l %d2-%d3/%a2-%a3,(%sp)
    move.l 20(%sp),%a2
    move.l 24(%sp),%a3
    move.l 28(%sp),-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    bsr.w ip_save_original
    lea 12(%sp),%sp
    move.l %d0,%d3
    tst.l ip_sa_ready
    beq.s 1f
    move.l %d3,-(%sp)
    move.l %a3,-(%sp)
    move.l %a2,-(%sp)
    jsr ip_sa_saved
    move.l %d0,%d3
    lea 12(%sp),%sp
1:
    move.l %d3,%d0
    movem.l (%sp),%d2-%d3/%a2-%a3
    lea 16(%sp),%sp
    rts
ip_save_original:
    lea -36(%sp),%sp
    movem.l %d2-%d5/%a2-%a6,(%sp)
    jmp ip_save_resume

/* Sound initializer, a2 = the Sound. Every register is kept; the replayed
 * CLR sets the stock CCR (TST, MOVEM, LEA and ADDQ to An leave it to that). */
    .globl ip_init_site
ip_init_site:
    tst.l ip_sa_ready
    beq.s 1f
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l %a2,-(%sp)
    jsr ip_sa_reset
    addq.l #4,%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
1:
    clr.l 0x13c(%a2)
    lea 0x140(%a2),%a2
    jmp ip_init_resume

/* Machine-type write, a0 = the Sound, d2 = new and d6 = old type. Every
 * register is kept; the replayed compare sets the flags stock tests next. */
    .globl ip_type_site
ip_type_site:
    cmp.l %d6,%d2
    beq.s 1f
    tst.l ip_sa_ready
    beq.s 1f
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l %a0,-(%sp)
    jsr ip_sa_reset
    addq.l #4,%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
1:
    move.b %d2,0xa2(%a0)
    cmp.l %d6,%d2
    jmp ip_type_resume
