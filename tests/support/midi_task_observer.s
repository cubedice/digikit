/* Observe the gathered-message boundary, preserving the stock router call. */
    .text
    .even
    .macro observe
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l 24(%sp),-(%sp)
    move.l 24(%sp),-(%sp)
    move.l 24(%sp),-(%sp)
    jsr mi_observe
    lea 12(%sp),%sp
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    .endm
    .globl mi_observe_usb,mi_observe_din
mi_observe_usb:
    observe
    jmp mi_observe_usb_tail
mi_observe_din:
    observe
    jmp mi_observe_din_tail
