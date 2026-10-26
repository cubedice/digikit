/* DT2 OS 1.17 only. The two hook sites are inside interrupt-masked regions.
 * Both replace the same countdown store with a call, replay that store, take
 * a bounded snapshot, and return to the stock SR restore. No allocation,
 * queue insertion, DSP transfer, or UI code runs in these hooks.
 *
 * Addresses below were resolved from the hash-pinned 1.17 MAIN image; the
 * module's named hook anchors and port check the image before patching.
 * The native getter reports MASTER_CURRENT, while MASTER_NEXT schedules ahead.
 * PATTERN is an opaque scheduling pointer, not a bank/pattern number.
 */
    .equ FW_COUNTDOWN,      0x80004888
    .equ FW_TRANSPORT,      0x43164f94
    .equ FW_MASTER_NEXT,    0x43164f98
    .equ FW_SUBSTEP,        0x43164f9a
    .equ FW_MASTER_CURRENT, 0x43164f9c
    .equ FW_PATTERN,        0x4316501c
    .equ FW_LENGTH,         0x43165130
    .equ FW_SCHED_CLOCK,    0x43158f7c
    .equ FW_BLOCKS,         0x40966840
    .equ PP_MAGIC,          0x50504f53
    .equ PP_CHECK_SALT,     0x7b91d6e3

    .text
    .even
    .globl pp_start_hook
pp_start_hook:
    move.l  %d2,FW_COUNTDOWN
    addq.l  #1,pp_position+16       /* generation of successful start/resume */
    bra.s   pp_snapshot_hook

    .globl pp_seq_hook
pp_seq_hook:
    move.l  %d2,FW_COUNTDOWN
pp_snapshot_hook:
    lea     -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    bsr.w   pp_snapshot
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea     16(%sp),%sp
    rts

/* Private, called only with interrupts masked. Exported for bounded CF tests.
 * Word 10 is the snapshot's CF block, word 12 is the transmit CF block.
 * Event sequence counts snapshots (substeps), not musical steps.
 */
    .globl pp_snapshot
pp_snapshot:
    lea     pp_position,%a0
    addq.l  #1,12(%a0)
    move.l  FW_BLOCKS,%d0
    move.l  %d0,40(%a0)
    move.l  FW_SCHED_CLOCK,%d0
    move.l  %d0,44(%a0)
    clr.l   %d0
    move.w  FW_MASTER_CURRENT,%d0
    move.l  %d0,28(%a0)
    clr.l   %d0
    move.w  FW_MASTER_NEXT,%d0
    move.l  %d0,52(%a0)
    clr.l   %d0
    move.b  FW_SUBSTEP,%d0
    move.l  %d0,36(%a0)
    move.l  FW_PATTERN,%d0
    move.l  %d0,24(%a0)
    clr.l   32(%a0)
    clr.l   pp_snapshot_valid
    /* Bound the full fallback length read inside the CF DDR aperture. */
    cmpi.l  #0x40000000,%d0
    blo.s   pp_snapshot_done
    cmpi.l  #0x47fe248a,%d0
    bhi.s   pp_snapshot_done
    btst    #0,%d0
    bne.s   pp_snapshot_done
    move.l  %d0,%a1
    moveq   #0,%d0
    move.w  FW_LENGTH,%d0
    ext.l   %d0
    cmpi.l  #1,%d0
    bgt.s   pp_length_ready
    adda.l  #0x1db74,%a1
    move.w  (%a1),%d0
    ext.l   %d0
pp_length_ready:
    cmpi.l  #1,%d0
    ble.s   pp_length_unknown
    move.l  %d0,32(%a0)
pp_length_unknown:
    moveq   #1,%d0
    move.l  %d0,pp_snapshot_valid
pp_snapshot_done:
    rts

/* void pp_write_frame(u32 tx_queue, u32 suffix_entry).
 * Called by core/dsp-link from the audio-send ISR, including while the code
 * link is idle. Only the data halfwords of the reserved 16-word suffix are
 * written. The stock driver still owns every command halfword.
 * The writer briefly masks interrupts so the checksum and copied snapshot
 * cannot mix generations, irrespective of the caller's interrupt priority.
 * Transport is read here too, so pause/reset invalidates an older snapshot
 * even when the sequencer no longer runs. External-clock stalls retain the
 * last position; clients must observe event sequence / snapshot age.
 */
    .globl pp_write_frame
pp_write_frame:
    lea     -12(%sp),%sp
    movem.l %d2-%d3/%a2,(%sp)
    move.w  %sr,%d3
    move.w  #0x2700,%sr
    lea     pp_position,%a0
    move.l  #PP_MAGIC,(%a0)
    moveq   #1,%d0
    move.l  %d0,4(%a0)
    addq.l  #1,8(%a0)
    move.l  FW_BLOCKS,%d0
    move.l  %d0,48(%a0)
    move.l  FW_TRANSPORT,%d0
    move.l  %d0,56(%a0)
    moveq   #0,%d1
    cmpi.l  #1,%d0
    bne.s   pp_flags_ready
    moveq   #2,%d1
    tst.l   pp_snapshot_valid
    beq.s   pp_flags_ready
    addq.l  #1,%d1
pp_flags_ready:
    move.l  %d1,20(%a0)
    move.l  #PP_CHECK_SALT,%d1
    moveq   #15,%d2
    move.l  %a0,%a1
pp_checksum:
    move.l  (%a1)+,%d0
    eor.l   %d0,%d1
    subq.l  #1,%d2
    bne.s   pp_checksum
    move.l  %d1,(%a1)
    move.l  16(%sp),%a1            /* tx_queue after saved registers */
    move.l  20(%sp),%d0            /* first reserved PUSHR entry */
    lsl.l   #2,%d0
    adda.l  %d0,%a1
    addq.l  #2,%a1                 /* data half; preserve command half */
    moveq   #16,%d2
pp_write_word:
    move.l  (%a0)+,%d0
    move.w  %d0,(%a1)
    swap    %d0
    move.w  %d0,4(%a1)
    addq.l  #8,%a1
    subq.l  #1,%d2
    bne.s   pp_write_word
    move.w  %d3,%sr
    movem.l (%sp),%d2-%d3/%a2
    lea     12(%sp),%sp
    rts

    .bss
    .balign 16
    .globl pp_position
pp_position:
    .space 64
pp_snapshot_valid:
    .space 4
    .space 12                     /* whole writable cache lines */
