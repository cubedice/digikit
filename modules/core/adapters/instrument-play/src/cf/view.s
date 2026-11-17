/* DT2 persistent machine view, as in the archived registry hook (finding 30).
 * Only remembers the C ABI argument; all registers and the stack are preserved. */
    .text
    .even
    .globl ip_view_entry
ip_view_entry:
    move.l %d0,-(%sp)
    move.l 8(%sp),%d0
    move.l %d0,ip_view
    move.l (%sp)+,%d0
    jmp ip_view_next
    .bss
    .balign 16
    .globl ip_view
ip_view: .space 16
