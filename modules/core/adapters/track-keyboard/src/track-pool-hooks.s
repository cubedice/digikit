    .text
    .even
    .globl tk_pool_stock_dispatch
tk_pool_stock_dispatch:
    move.l 4(%sp),-(%sp)
    jsr tk_pool_fw_block_note
    addq.l #4,%sp
    tst.l %d0
    bne.s 1f
    lea -28(%sp),%sp
    movem.l %d2-%d4/%a2-%a5,(%sp)
    jmp tk_pool_stock_dispatch_resume
1:  rts
