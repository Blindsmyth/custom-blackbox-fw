@ Batch 1: hold PADS or SEQS and tap a pad to select it without playing it. While held,
@ each screen also shows its second side panel (Pads: CUT/COPY/PSTE/CLR, Seq: layers A-D),
@ since the encoder that used to swap panels is off.
@ PADS is button-table entry 0, SEQS entry 2 (debounced level +0xE, 0 = pressed).
@ - Pads onChildEvent (0x080B3E84) lets 3/4 propagate to App_HandleUiEvent (select + play).
@   While PADS is held, a press posts 0x63 (select) instead and the release is swallowed.
@ - Seq onChildEvent (0x080B4DEC) posts 0xFA (toggle play) / 0xFB. While SEQS is held, a
@   press posts 0xFC (select only, FUN_08098328) and the release is swallowed.

.equ BTN_PADS,    0x24009ABE
.equ BTN_SEQS,    0x24009AEE
.equ PADS_CHILD,  0x080B3E85
.equ SEQ_CHILD,   0x080B4DED
.equ PADS_TICK,   0x080B476D
.equ PADS_PANEL_A, 0x2110               @ VEL fader
.equ PADS_PANEL_B, 0x21F0               @ CUT / COPY / PSTE / CLR
.equ MX_PADS_PANEL, 44                  @ MX_STATE: 1 while the edit panel is shown

.global pads_child_event
.global pads_tick
.global hold_panel
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

@ hold_panel(widget r0, flags r1, button level r2, state slot r3, [sp] panel A offset,
@ [sp+4] panel B offset): while the button is held show panel B instead of panel A (stock
@ swaps them by their hidden bytes +0x30). Asks for both redraw passes on a change.
.thumb_func
hold_panel:
    push    {r4, r5, r6, r7, lr}
    mov     r4, r0
    mov     r5, r1
    ldrb    r6, [r2]
    cmp     r6, #0
    ite     eq
    moveq   r6, #1                  @ level 0 = held
    movne   r6, #0
    mov     r7, r3
    bl      mx_state
    ldr     r1, [r0, r7]
    cmp     r1, r6
    beq     9f
    str     r6, [r0, r7]
    ldr     r0, [sp, #20]           @ panel A offset
    add     r0, r4
    strb    r6, [r0, #0x30]
    eor     r1, r6, #1
    ldr     r0, [sp, #24]           @ panel B offset
    add     r0, r4
    strb    r1, [r0, #0x30]
    movs    r0, #1
    strb    r0, [r5]
    strb    r0, [r5, #1]
9:
    pop     {r4, r5, r6, r7, pc}

@ tick(pads, flags): stock, then the edit panel while PADS is held.
.thumb_func
pads_tick:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    call    PADS_TICK
    movw    r0, #PADS_PANEL_B
    movw    r1, #PADS_PANEL_A
    push    {r0, r1}                @ [sp] = A, [sp+4] = B after the swap below
    str     r1, [sp]
    str     r0, [sp, #4]
    mov     r0, r4
    mov     r1, r5
    ldr     r2, =BTN_PADS
    movs    r3, #MX_PADS_PANEL
    bl      hold_panel
    add     sp, #8
    pop     {r4, r5, r6, pc}

.align 2
.ltorg
