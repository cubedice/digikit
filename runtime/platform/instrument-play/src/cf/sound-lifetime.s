/* Unlinked DTII 1.17 Sound-handle invalidation prototypes.
 * These are not sufficient to make the gap a safe ownership field: successful
 * Sound loads, other type writes, copies and allocation failures need separate
 * handling. The host contract requires serialized access to the Sound itself.
 *
 * Initializer site 0x400e65c8: a2 = non-null Sound, replay the displaced
 * clr.l 0x13c(a2); lea 0x140(a2),a2. The CLR establishes the same CCR
 * as stock, including preserving X, and LEA/JMP do not change the CCR. */
    .text
    .even
    .globl ip_sound_init_gap
ip_sound_init_gap:
    move.w #0xffff,0x13a(%a2)
    clr.l 0x13c(%a2)
    lea 0x140(%a2),%a2
    jmp 0x400e65d0

/* Type setter site 0x4005179e: a0 = Sound, d2 = requested type, d6 = old
 * signed type. Marking before changing the type prevents the stale handle
 * from following this observed machine-change path. The final compare exactly
 * reproduces stock's branch flags, while a no-op type assignment retains the
 * handle. MOVEs leave the X flag untouched. */
    .globl ip_sound_type_gap
ip_sound_type_gap:
    cmp.l %d6,%d2
    beq.s 1f
    move.w #0xffff,0x13a(%a0)
1:
    move.b %d2,0xa2(%a0)
    cmp.l %d6,%d2
    jmp 0x400517a4
