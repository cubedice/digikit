/* The stock helper reads only the low byte of its argument. Save volatile
 * registers and preserve the argument/return stack before the displaced entry.
 * The expansion loader has initialized code/data/BSS before this task runs. */
    .text
    .even
    .globl mi_dsp_reset_hook
mi_dsp_reset_hook:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    tst.b 23(%sp)
    beq.s 1f
    jsr mi_dsp_reset_begin
1:  movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    jmp mi_dsp_reset_next
