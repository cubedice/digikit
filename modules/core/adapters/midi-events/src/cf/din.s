/* DTII 1.17 DIN router call, same (msg,len,source) stack as the USB hook.
 * Always restore volatile registers before continuing in the stock task.
 * The stock tail pops arguments regardless of whether the message was claimed. */
    .text
    .even
    .globl mi_din_hook
mi_din_hook:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l 24(%sp),-(%sp)
    move.l 24(%sp),-(%sp)
    move.l 24(%sp),-(%sp)
    jsr mi_channel_message
    lea 12(%sp),%sp
    tst.l %d0
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    bne.s 1f
    jmp mi_din_next
1:  jmp mi_din_return
