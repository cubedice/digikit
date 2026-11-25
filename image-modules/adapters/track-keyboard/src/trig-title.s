/* DTII 1.17 ParameterPageView title: the caller supplies the one-word
 * std::string result in A0, separately from the view passed on the stack. */
    .text
    .even
    .globl tk_trig_title
tk_trig_title:
    move.l 4(%sp),%a1
    move.l 0x90(%a1),%d0
    cmpi.l #2,%d0
    beq.s .Larp
    cmpi.l #3,%d0
    beq.s .Lkeys
    jmp tk_trig_stock_title
.Larp:
    lea .Larp_title,%a1
    bra.s .Lconstruct
.Lkeys:
    lea .Lkeys_title,%a1
.Lconstruct:
    move.l %a0,-(%sp)
    clr.l -(%sp)
    pea (%sp)
    move.l %a1,-(%sp)
    move.l %a0,-(%sp)
    jsr tk_trig_title_ctor
    lea 16(%sp),%sp
    move.l (%sp)+,%d0
    rts

    .section .rodata
.Larp_title:
    .asciz "Arpeggiator (3/4)"
.Lkeys_title:
    .asciz "Keyboard (4/4)"
