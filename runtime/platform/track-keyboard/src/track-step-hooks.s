/* Displaced DTII 1.17 prologues. The C wrappers call these continuations
 * so clipboard snapshots can use the native operations without re-entry.
 */
    .text
    .even
    .globl tk_stock_step_copy
tk_stock_step_copy:
    link.w %a6,#-136
    movem.l %d2-%d7/%a2-%a5,(%sp)
    jmp tk_step_copy_resume

    .even
    .globl tk_stock_step_paste
tk_stock_step_paste:
    link.w %a6,#-132
    lea -92(%a6),%a0
    jmp tk_step_paste_resume

    .even
    .globl tk_stock_step_clear
tk_stock_step_clear:
    lea -24(%sp),%sp
    movem.l %d2-%d4/%a2-%a4,(%sp)
    jmp tk_step_clear_resume

    .even
    .globl tk_stock_step_toggle
tk_stock_step_toggle:
    lea -16(%sp),%sp
    movem.l %d2-%d4/%a2,(%sp)
    jmp tk_step_toggle_resume

    .even
    .globl tk_stock_step_clip_get
tk_stock_step_clip_get:
    move.l 4(%sp),%a0
    jmp tk_step_clip_get_entry
