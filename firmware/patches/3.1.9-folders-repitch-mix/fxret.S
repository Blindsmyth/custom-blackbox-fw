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
    sub     sp, #16
    add.w   r0, r4, #FXR_BAR        @ bar along the bottom edge, like the pad page tabs
    adr     r1, fxr_bar_rect
    call    W_SETRECT
    movs    r6, #0
1:
    adr     r0, fxr_btns
    ldrh.w  r0, [r0, r6, lsl #1]
    add     r0, r4                  @ button
    movw    r1, #FXR_ID
    add     r1, r6
    str     r1, [r0, #0x14]         @ id: 0x7A0 + position
    adr     r1, fxr_x
    ldrh.w  r2, [r1, r6, lsl #1]
    str     r2, [sp]                @ x
    movs    r2, #0
    str     r2, [sp, #4]
    add.w   r1, r1, r6, lsl #1
    ldrh    r2, [r1, #2]
    ldrh    r1, [r1]
    subs    r2, r2, r1
    str     r2, [sp, #8]            @ w
    movs    r1, #FXR_BAR_H
    str     r1, [sp, #12]
    push    {r0, r1}
    mov     r1, sp
    adds    r1, #8
    ldr     r2, [r0]
    ldr     r2, [r2, #0x20]
    blx     r2                      @ setRect
    ldr     r0, [sp]
    adr     r1, fxr_labels
    ldr     r1, [r1, r6, lsl #2]
    call    BTN_TEXT
    pop     {r0, r1}
    movs    r1, #0
    cmp     r6, #1
    bhi     2f
    movw    r3, #0x300
    add.w   r3, r3, r6, lsl #4      @ the return this button shows
    cmp     r3, r5
    it      eq
    moveq   r1, #1
2:
    call    BTN_SELECT
    adds    r6, #1
    cmp     r6, #4
    blo     1b
    add     sp, #16
9:
    pop     {r4, r5, r6, pc}

@ Buttons left to right: the three bar tabs and the old FX-name cell (stock writes the name
@ there on every rebuild; we overwrite it with an empty label).
.align 2
fxr_btns:
    .hword  FXR_TABS + 2 * FXR_TAB_SZ, FXR_TABS + FXR_TAB_SZ, FXR_TABS, 0x3AC
fxr_labels:
    .word   s_ra, s_rb, s_eq, s_none
fxr_x:                              @ button edges: labels are 12 px per character
    .hword  0, 104, 216, 268, 320
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
    bne     2f
    movs    r0, #0                  @ the empty fourth button does nothing
    bx      lr
2:
    push    {r4, lr}
    ldr     r0, =APP
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
.align 2
.ltorg
