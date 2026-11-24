/* cf.page.hook.phrase_key_walk (ports/fw/phrase-ui.toml): jumped to from the key walker, after its prologue, with its frame. SP: D2-D7/A2-A6
 * saved at 0(SP) (0x60 bytes in all), the walker's return address at 0x60(SP), the ViewController at 0x64(SP), the KeyEvent at 0x68(SP).
 * fxv_key_filter(vc, event) (native.c) decides: 0 passes (the two displaced moves, then the rest of the walker, D0, D1, A0 and A1 as they
 * were), 1 consumes (the walker returns true as its epilogue would: D2-D7/A2-A6 restored, the frame dropped). */
    .text
    .globl fxv_walk_hook
fxv_walk_hook:
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    move.l 0x78(%sp),-(%sp)         /* the KeyEvent (0x68 + 16) */
    move.l 0x78(%sp),-(%sp)         /* the ViewController (0x64 + 16 + 4) */
    jsr fxv_key_filter
    addq.l #8,%sp
    tst.l %d0
    bne.s 1f
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    move.l 0x68(%sp),%d2            /* the displaced instructions */
    movea.l 0x64(%sp),%a2
    jmp fxv_walk_back
1:
    lea 16(%sp),%sp
    movem.l (%sp),%d2-%d7/%a2-%a6
    lea 0x60(%sp),%sp
    moveq #1,%d0
    rts
