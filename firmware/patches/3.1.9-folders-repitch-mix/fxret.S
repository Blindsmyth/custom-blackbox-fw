@ Batch 1: FX button toggles DJ FX (0x37) and the FX Return page (0x23); EQ (0x36) and the
@ send page (0x30) leave the cycle. The Return page's three blank tabs (button bar at
@ page+0x34, buttons at page+0x70 + i*0xA0) become A (Delay 0x300), B (Reverb 0x310) and
@ EQ, left to right, in a four-button row along the bottom (the fourth is empty), like the
@ pad page's Main / Pos / LFO / Conf. A and B are wider so "A Delay" / "B Reverb" fit. App+0x28 is the FX key the page shows; the page rebuilds on App_SetScreen(0x23).

.equ FXR_EVENT,   0x080AB901
.equ FXR_CHILD,   0x080AB9B5
.equ BTN_SELECT,  0x080A3E01            @ (button, on)
.equ APP_FXKEY,   0x28
.equ FXR_TABS,    0x70
.equ FXR_TAB_SZ,  0xA0
.equ FXR_ID,      0x7A0                 @ our tab ids: 0x7A0 A, 0x7A1 B, 0x7A2 EQ
.equ MX_FX_LAST,  28                    @ MX_STATE: last return shown
.equ FXR_BAR,     0x34
.equ FXR_BAR_H,   38
.equ W_SETRECT,   0x080A3715            @ generic widget setRect (bar)

.global fx_to_return
.global fxret_on_event
.global fxret_child_event
.global fx_row_button

@ r0 = app. Returns r0 = 0x300 or 0x310: App+0x28 if it is a return, else the last one.
.thumb_func
fx_return_key:
    ldrh    r1, [r0, #APP_FXKEY]
    cmp     r1, #0x300
    beq     1f
    cmp     r1, #0x310
    beq     1f
    push    {r0, lr}
    bl      mx_state
    ldr     r1, [r0, #MX_FX_LAST]
    pop     {r0, lr}
    cmp     r1, #0x310
    it      ne
    movne   r1, #0x300
1:
    strh    r1, [r0, #APP_FXKEY]
    mov     r0, r1
    bx      lr

@ bl site 0x080A32BE (was App_SetScreen(app, 0x30) from DJ FX).
.thumb_func
fx_to_return:
    push    {r0, r1, r2, r3, r4, lr}
    bl      fx_return_key
    pop     {r0, r1, r2, r3, r4, lr}
    movs    r1, #0x23
    ldr     r12, =SCREEN
    bx      r12

.thumb_func
fxret_on_event:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldrh    r5, [r1]
    call    FXR_EVENT
    sub     r5, #0x66
    cmp     r5, #1
    bhi     9f
    ldr     r0, =APP
    ldrh    r5, [r0, #APP_FXKEY]
    bl      mx_state
    cmp     r5, #0x300
    it      ne
    cmpne   r5, #0x310
    it      eq
    streq   r5, [r0, #MX_FX_LAST]
    add.w   r0, r4, #FXR_BAR        @ bar along the bottom edge, like the pad page tabs
    adr     r1, fxr_bar_rect
    call    W_SETRECT
    movs    r6, #0
1:
    adr     r0, fxr_btns
    ldrh.w  r0, [r0, r6, lsl #1]
    add     r0, r4                  @ button
    movs    r2, #0
    cmp     r6, #1
    bhi     2f
    movw    r3, #0x300
    add.w   r3, r3, r6, lsl #4      @ the return this button shows
    cmp     r3, r5
    it      eq
    moveq   r2, #1
2:
    mov     r1, r6
    bl      fx_row_button
    adds    r6, #1
    cmp     r6, #4
    blo     1b
9:
    pop     {r4, r5, r6, pc}

@ fx_row_button(button r0, position r1 (0..3), lit r2): place one button of the bottom row
@ (A Delay | B Reverb | EQ | empty), with id 0x7A0 + position, its label and highlight.
.thumb_func
fx_row_button:
    push    {r4, r5, r6, lr}
    sub     sp, #16
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    movw    r1, #FXR_ID
    add     r1, r5
    str     r1, [r4, #0x14]         @ id
    adr     r1, fxr_x
    add.w   r1, r1, r5, lsl #1
    ldrh    r2, [r1]
    str     r2, [sp]                @ x
    ldrh    r3, [r1, #2]
    subs    r3, r3, r2
    str     r3, [sp, #8]            @ w
    movs    r2, #0
    str     r2, [sp, #4]            @ y: bottom edge
    movs    r2, #FXR_BAR_H
    str     r2, [sp, #12]
    mov     r0, r4
    mov     r1, sp                  @ stack rect: word aligned for the LDM in setRect
    ldr     r2, [r4]
    ldr     r2, [r2, #0x20]
    blx     r2
    mov     r0, r4
    adr     r1, fxr_labels
    ldr     r1, [r1, r5, lsl #2]
    call    BTN_TEXT
    mov     r0, r4
    mov     r1, r6
    call    BTN_SELECT
    add     sp, #16
    pop     {r4, r5, r6, pc}

@ Buttons left to right: the three bar tabs and the old FX-name cell (stock writes the name
@ there on every rebuild; we overwrite it with an empty label).
.align 2
fxr_btns:
    .hword  FXR_TABS + 2 * FXR_TAB_SZ, FXR_TABS + FXR_TAB_SZ, FXR_TABS, 0x3AC
fxr_labels:
.ifdef WARP_LINEUP
    .word   s_ra_short, s_rb_short, s_eq, s_master_tab  @ 3.1.T: Master (Tools page 4) next to EQ
.else
    .word   s_ra, s_rb, s_eq, s_none
.endif
fxr_x:                              @ button edges: labels are 12 px per character
.ifdef WARP_LINEUP
    .hword  0, 80, 160, 240, 320    @ A Dly | B Rvb | EQ | Master, 80 px each
.else
    .hword  0, 104, 216, 268, 320
.endif
@ LDM-read by the stock setRect: must be word aligned (an unaligned LDM hard-faults).
.align 2
fxr_bar_rect:
    .word   0, 0, 320, FXR_BAR_H

@ Tab press (child event 1, id at +0xC): A/B show that return, EQ opens the EQ page.
.thumb_func
fxret_child_event:
    ldrh    r2, [r1]
    cmp     r2, #1
    bne     9f
    ldr     r2, [r1, #0xC]
    movw    r3, #FXR_ID
    subs    r2, r2, r3
    cmp     r2, #3
    bhi     9f
.ifdef WARP_LINEUP
    push    {r4, lr}
    ldr     r0, =APP
    movs    r1, #0x29               @ Master: Tools page 4, shown with this row (dj.c)
    cmp     r2, #3
    beq     1f
.else
    bne     2f
    movs    r0, #0                  @ the empty fourth button does nothing
    bx      lr
2:
    push    {r4, lr}
    ldr     r0, =APP
.endif
    movs    r1, #0x36
    cmp     r2, #2
    beq     1f
    movw    r1, #0x300
    add.w   r1, r1, r2, lsl #4
    strh    r1, [r0, #APP_FXKEY]
    push    {r0, r1}
    bl      mx_state
    pop     {r2, r3}
    str     r3, [r0, #MX_FX_LAST]
    mov     r0, r2
    movs    r1, #0x23
1:
    movs    r2, #0
    movs    r3, #0
    call    SCREEN
    movs    r0, #0
    pop     {r4, pc}
9:
    ldr     r12, =FXR_CHILD
    bx      r12

.align 2
s_ra: .asciz "A Delay"
s_rb: .asciz "B Reverb"
s_eq: .asciz "EQ"
s_none: .asciz ""
.ifdef WARP_LINEUP
s_master_tab: .asciz "Master"
s_ra_short: .asciz "A Dly"
s_rb_short: .asciz "B Rvb"
.endif
.align 2
.ltorg

@ ---- EQ page (0x36): the same row along the bottom, the EQ view 40 px shorter.
@ The page has no spare buttons, so four frame buttons (ctor 0x080A3D34, 0xA0 bytes) live in
@ SRAM4 at EQ_BTNS and are attached once per boot after the page ctor. The layout
@ (FUN_080AA8D2, on every show) passes the graph rect on the stack to the graph setRect at
@ 0x080AA8F8; the value boxes and the ON button read y/h back from it, so shrinking it there
@ moves them all.

.equ EQ_BTNS,     0x38005400            @ 4 x 0xA0, past MX_STATE
.equ EQ_CTOR,     0x080AA291
.equ EQ_GRAPH_RECT, 0x080AA075
.equ EQ_CHILD,    0x080AA24D
.equ FRAME_CTOR,  0x080A3D35
.equ EQ_ROW_H,    40

.global eq_page_ctor
.global eq_graph_rect
.global eq_child_event
.global eq_fix_defaults

@ bl site 0x0808BF20 (GUI ctor -> EQ page ctor).
.thumb_func
eq_page_ctor:
    push    {r4, r5, r6, lr}
    call    EQ_CTOR
    mov     r4, r0                  @ page (the ctor returns this)
    ldr     r5, =EQ_BTNS
    movs    r6, #0
1:
    mov     r0, r5
    movs    r1, #0
    movs    r2, #0xA0
    call    MEMSET
    mov     r0, r5
    call    FRAME_CTOR
    mov     r0, r4
    mov     r1, r5
    call    ADD_CHILD
    adds    r5, #0xA0
    adds    r6, #1
    cmp     r6, #4
    blo     1b
    mov     r0, r4
    pop     {r4, r5, r6, pc}

@ bl site 0x080AA8F8: graph setRect(graph r0, rect r1 on the layout's stack).
.thumb_func
eq_graph_rect:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldr     r2, [r1, #4]
    adds    r2, #EQ_ROW_H
    str     r2, [r1, #4]
    ldr     r2, [r1, #12]
    subs    r2, #EQ_ROW_H
    str     r2, [r1, #12]
    call    EQ_GRAPH_RECT
    sub     r0, r4, #0x34           @ page
    bl      eq_fix_defaults
    ldr     r5, =EQ_BTNS
    movs    r6, #0
1:
    mov     r0, r5
    mov     r1, r6
    cmp     r6, #2
    ite     eq
    moveq   r2, #1                  @ EQ lit on the EQ page
    movne   r2, #0
    bl      fx_row_button
    adds    r5, #0xA0
    adds    r6, #1
    cmp     r6, #4
    blo     1b
    pop     {r4, r5, r6, pc}

@ eq_fix_defaults(page): an EQ nobody has touched (all bands None, gain 0, stock
@ frequencies 200/400/600/800) gets Low Shelf / Param / Param / High Shelf. New EQs get that
@ from the template patch; this covers presets saved before it.
.thumb_func
eq_fix_defaults:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #12
    movw    r1, #0x1BCC
    ldrh    r1, [r0, r1]            @ the EQ's key
    cmp     r1, #0
    it      eq
    moveq   r1, #0x320              @ master EQ slot
    ldr     r2, =REF_TAG
    str     r2, [sp]
    strh    r1, [sp, #4]
    movs    r4, #0                  @ band
    movw    r5, #0x118              @ band's first id (gain)
1:
    ldr     r0, =APP
    mov     r1, sp
    adds    r2, r5, #4              @ type
    bl      sr_getp
    cbnz    r0, 9f
    ldr     r0, =APP
    mov     r1, sp
    mov     r2, r5                  @ gain
    bl      sr_getp
    cbnz    r0, 9f
    ldr     r0, =APP
    mov     r1, sp
    adds    r2, r5, #1              @ freq
    bl      sr_getp
    adds    r1, r4, #1
    movs    r2, #200
    mul     r1, r1, r2
    cmp     r0, r1
    bne     9f
    adds    r4, #1
    adds    r5, #5
    cmp     r4, #4
    blo     1b
    movs    r4, #0
    movw    r5, #0x11C
2:
    adr     r3, eq_default_types
    ldrb    r3, [r3, r4]
    ldr     r0, =APP
    mov     r1, sp
    mov     r2, r5
    bl      sr_setp
    adds    r4, #1
    adds    r5, #5
    cmp     r4, #4
    blo     2b
9:
    add     sp, #12
    pop     {r4, r5, r6, r7, pc}

.align 2
eq_default_types:
    .byte   2, 3, 3, 4                  @ L Shelf, Param, Param, H Shelf

@ EQ onChildEvent: row taps; A/B open that return, EQ and the empty button do nothing.
.thumb_func
eq_child_event:
    ldrh    r2, [r1]
    cmp     r2, #1
    bne     9f
    ldr     r2, [r1, #0xC]
    movw    r3, #FXR_ID
    subs    r2, r2, r3
    cmp     r2, #3
    bhi     9f
    cmp     r2, #2
    beq     8f
    b.w     fxret_child_event       @ A, B, empty: same as on the Return page
8:
    movs    r0, #0
    bx      lr
9:
    ldr     r12, =EQ_CHILD
    bx      r12

.align 2
.ltorg
