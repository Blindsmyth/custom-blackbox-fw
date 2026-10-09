@ Batch 1: FX button toggles DJ FX (0x37) and the FX Return page (0x23); EQ (0x36) and the
@ send page (0x30) leave the cycle. The Return page's three blank tabs (button bar at
@ page+0x34, buttons at page+0x70 + i*0xA0) become A (Delay 0x300), B (Reverb 0x310) and
@ EQ, left to right. App+0x28 is the FX key the page shows; the page rebuilds on App_SetScreen(0x23).

.equ FXR_EVENT,   0x080AB901
.equ FXR_CHILD,   0x080AB9B5
.equ BTN_SELECT,  0x080A3E01            @ (button, on)
.equ APP_FXKEY,   0x28
.equ FXR_TABS,    0x70
.equ FXR_TAB_SZ,  0xA0
.equ FXR_ID,      0x7A0                 @ our tab ids: 0x7A0 A, 0x7A1 B, 0x7A2 EQ
.equ MX_FX_LAST,  28                    @ MX_STATE: last return shown

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
    movs    r6, #0
1:
    movs    r0, #FXR_TAB_SZ
    mul     r0, r6, r0
    add     r0, #FXR_TABS
    add     r0, r4
    adr     r1, fxr_tab_ids
    ldrh.w  r1, [r1, r6, lsl #1]
    str     r1, [r0, #0x14]          @ button id
    adr     r1, fxr_labels
    ldr     r1, [r1, r6, lsl #2]
    push    {r0, r1}
    call    BTN_TEXT
    pop     {r0, r1}
    ldr     r2, [r0, #0x14]
    movw    r3, #FXR_ID
    subs    r2, r2, r3              @ 0 A, 1 B, 2 EQ
    movs    r1, #0
    cmp     r2, #1
    bhi     2f
    movw    r3, #0x300
    add.w   r3, r3, r2, lsl #4      @ the return this tab shows
    cmp     r3, r5
    it      eq
    moveq   r1, #1
2:
    call    BTN_SELECT
    adds    r6, #1
    cmp     r6, #3
    blo     1b
9:
    pop     {r4, r5, r6, pc}

@ The bar lays its buttons out right to left (tab 0 is third from the left, tab 2 is the
@ narrow cell on the left; the FX name cell is on the right), so on screen: A | B | EQ | name.
.align 2
fxr_labels:
    .word   s_eq, s_rb, s_ra
fxr_tab_ids:
    .hword  FXR_ID + 2, FXR_ID + 1, FXR_ID

@ Tab press (child event 1, id at +0xC): A/B show that return, EQ opens the EQ page.
.thumb_func
fxret_child_event:
    ldrh    r2, [r1]
    cmp     r2, #1
    bne     9f
    ldr     r2, [r1, #0xC]
    movw    r3, #FXR_ID
    subs    r2, r2, r3
    cmp     r2, #2
    bhi     9f
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
s_ra: .asciz "A"
s_rb: .asciz "B"
s_eq: .asciz "EQ"
.align 2
.ltorg
