@ Batch 1: hold PADS or SEQS and tap a pad to select it without playing it.
@ PADS is button-table entry 0, SEQS entry 2 (debounced level +0xE, 0 = pressed).
@ - Pads onChildEvent (0x080B3E84) lets 3/4 propagate to App_HandleUiEvent (select + play).
@   While PADS is held, a press posts 0x63 (select) instead and the release is swallowed.
@ - Seq onChildEvent (0x080B4DEC) posts 0xFA (toggle play) / 0xFB. While SEQS is held, a
@   press posts 0xFC (select only, FUN_08098328) and the release is swallowed.

.equ BTN_PADS,    0x24009ABE
.equ BTN_SEQS,    0x24009AEE
.equ PADS_CHILD,  0x080B3E85
.equ SEQ_CHILD,   0x080B4DED

.global pads_child_event
.global seq_child_event

@ r0 = widget, r1 = event, r2 = posted type, r3 = key or-mask. Posts {type, key} up and
@ returns 0 (handled).
.thumb_func
post_key:
    push    {r4, lr}
    sub     sp, #24
    strh    r2, [sp]
    ldr     r2, =EV_TAG
    str     r2, [sp, #4]
    ldrh    r2, [r1, #8]
    cbz     r3, 1f
    movw    r4, #0xE0FF
    ands    r2, r4
    orrs    r2, r3
1:
    strh    r2, [sp, #8]
    movs    r2, #0
    str     r2, [sp, #12]
    str     r2, [sp, #16]
    str     r2, [sp, #20]
    mov     r1, sp
    call    UI_POST_UP
    movs    r0, #0
    add     sp, #24
    pop     {r4, pc}

.thumb_func
pads_child_event:
    ldrh    r2, [r1]
    subs    r2, #3
    cmp     r2, #1
    bhi     9f
    ldr     r3, =BTN_PADS
    ldrb    r3, [r3]
    cbnz    r3, 9f                  @ not held: stock (select + play)
    cbnz    r2, 8f                  @ release: swallow
    movs    r2, #0x63
    movs    r3, #0
    b       post_key
8:
    movs    r0, #0
    bx      lr
9:
    ldr     r12, =PADS_CHILD
    bx      r12

.thumb_func
seq_child_event:
    ldrh    r2, [r1]
    subs    r2, #3
    cmp     r2, #1
    bhi     9f
    ldr     r3, =BTN_SEQS
    ldrb    r3, [r3]
    cbnz    r3, 9f
    cbnz    r2, 8f
    movs    r2, #0xFC
    mov     r3, #0x100
    b       post_key
8:
    movs    r0, #0
    bx      lr
9:
    ldr     r12, =SEQ_CHILD
    bx      r12

.align 2
.ltorg
