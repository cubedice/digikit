/* Replayed entry instructions, pinned to DT2 OS1.17 by the hook windows. */
    .text
    .even
    .globl ip_panel_original_on
ip_panel_original_on:
    link.w %a6,#-68
    moveq #127,%d0
    jmp ip_panel_on_resume
    .globl ip_panel_original_off
ip_panel_original_off:
    lea -24(%sp),%sp
    movem.l %d2/%a2-%a6,(%sp)
    jmp ip_panel_off_resume
