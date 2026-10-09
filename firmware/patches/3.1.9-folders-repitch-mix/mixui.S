@ 3.1.Q Mix screen overhaul. Map: docs/map-319.md "Mix and Mute".
@
@ Mix and Mute are one widget (vtable 0x080F0F10, mode byte +0x1E40). Three vtable words
@ point here (tick, onChildEvent, onEvent) and two App_HandleInput calls to App_SetScreen
@ are redirected (MIX on Mix, INFO on Mix).
@
@ - Pads play and record on Mix: pad press/release (child events 3/4) keep propagating to
@   App_HandleUiEvent like on the Pads screen. While MIX is held they go to the stock
@   handler, which turns a press into 0x63 (select, no trigger).
@ - The four stock side faders (class 0x080C1F3C, built by the Mix constructor but never
@   attached) become children and replace the empty frames. Physical layout (the GUI's
@   y axis runs bottom-up, so logical y=1 is the physical bottom row):
@       Cutoff 0xC8  | Decay  0x3E       top row      (faders +0x2C48 | +0x2DA0)
@       Send A 0xD9  | Send B 0xDA       bottom row   (faders +0x2CF4 | +0x2E4C)
@   A fader's own touch handler posts 0x6E {pad +0x38, id, value} -> App_SetParam.
@ - While MIX is held the left column is one tall Vol (0x04) fader and the right column
@   one tall Pan (0x62) fader.
@ - Knobs drive the four stock value-engine sliders, bound to the same params, so step,
@   acceleration and App_SetParam stay stock. Logical encoder order is column-major:
@   0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right.
@ - INFO on Mix opens Mute. Released within MX_HOLD_MS it stays (toggle); held longer it
@   goes back to Mix on release (momentary).
@
@ UI task only (App_HandleInput, GUI tick/event). RAM only. MIX and INFO send no release
@ event, so hold state is the debounced level byte in the button table (0 = pressed).

.equ MX_STATE,    0x38005200        @ past the Warp flags (0x38005000..0x38005110)
.equ MX_MAGIC,    0x5558494D        @ 'MIXU'
.equ MX_HELD,     4                 @ layout applied: 0 normal, 1 MIX held, 0xFF none
.equ MX_INFO,     8                 @ 0 idle, 1 INFO pressed on Mix, 2 momentary
.equ MX_T0,       12                @ uwTick when INFO opened Mute
.equ MX_HOLD_MS,  400

.equ APP,         0x24020088
.equ UWTICK,      0x24015CE4
.equ BTN_MIX,     0x24009B36        @ button table entry 5 +0xE
.equ BTN_INFO,    0x24009BDE        @ button table entry 12 +0xE
.equ REF_TAG,     0x080EAF38

.equ MIX_TICK,    0x080B6601
.equ MIX_CHILD,   0x080B5A4D
.equ MIX_EVENT,   0x080B5C71
.equ ADD_CHILD,   0x080AEC45
.equ FADER_SET,   0x080C20D1        @ (fader, id, value)
.equ FADER_RECT,  0x080C1EE5        @ fader setRect (also places its label)
.equ SLIDER_BIND, 0x080A7EE1        @ (id, value, slider, 0)
.equ SLIDER_STEP, 0x080B0D55        @ (slider, delta)
.equ GET_PARAM,   0x08099505        @ (app, ref, id, &out)
.equ SET_TEXT,    0x080C3B99        @ ui_draw_cstring(label, str)

.equ M_MODE,      0x1E40
.equ M_SEL,       0x1E08
.equ F_CUT,       0x2C48
.equ F_DEC,       0x2DA0
.equ F_SA,        0x2CF4
.equ F_SB,        0x2E4C
.equ W_HIDDEN,    0x30
.equ W_PARENT,    0x2C
.equ F_PAD,       0x38
.equ F_LABEL,     0x54

.global mix_tick
.global mix_child_event
.global mix_on_event
.global mix_btn_screen
.global info_to_mute

@ r0 = MX_STATE, initialised if the magic is missing. Clobbers r1.
.thumb_func
mx_state:
    ldr     r0, =MX_STATE
    ldr     r1, [r0]
    push    {r2}
    ldr     r2, =MX_MAGIC
    cmp     r1, r2
    beq     1f
    str     r2, [r0]
    movs    r1, #0xFF
    str     r1, [r0, #MX_HELD]
    movs    r1, #0
    str     r1, [r0, #MX_INFO]
    str     r1, [r0, #MX_T0]
1:
    pop     {r2}
    bx      lr

@ ---- vtable +0x30: pads play unless MIX is held

.thumb_func
mix_child_event:
    ldrh    r2, [r1]
    subs    r2, #3
    cmp     r2, #1
    bhi     1f
    add.w   r2, r0, #0x1000
    ldrb.w  r2, [r2, #(M_MODE - 0x1000)]
    cbnz    r2, 1f
    ldr     r2, =BTN_MIX
    ldrb    r2, [r2]
    cbz     r2, 1f                  @ MIX held: stock select-only
    movs    r0, #1                  @ keep propagating to App_HandleUiEvent
    bx      lr
1:
    ldr     r12, =MIX_CHILD
    bx      r12

@ ---- vtable +0x34: knobs, and refresh the faders after stock

.thumb_func
mix_on_event:
    ldrh    r2, [r1]
    cmp     r2, #0x32
    bne     2f
    ldrh    r2, [r1, #0xC]
    cmp     r2, #3
    bhi     9f
    adr     r3, knob_slider
    ldrh.w  r2, [r3, r2, lsl #1]
    add     r0, r2
    ldrsh.w r1, [r1, #0x10]
    ldr     r12, =SLIDER_STEP
    bx      r12
2:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r2
    call    MIX_EVENT
    cmp     r5, #0x63
    bne     3f
    mov     r0, r4
    bl      bind_knobs
    b       4f
3:
    cmp     r5, #0x66
    beq     4f
    cmp     r5, #0x67
    bne     5f
4:
    mov     r0, r4
    bl      show_values
5:
    pop     {r4, r5, r6, pc}
9:
    bx      lr

@ Logical encoder -> value slider. Sliders: +0x1E48 Cutoff, +0x2128 Decay,
@ +0x2408 Send A, +0x26E8 Send B.
.align 2
knob_slider:
    .hword  0x1E48                  @ 0 top-left     Cutoff
    .hword  0x2408                  @ 1 bottom-left  Send A
    .hword  0x2128                  @ 2 top-right    Decay
    .hword  0x26E8                  @ 3 bottom-right Send B

@ (slider offset, param id)
.align 2
slider_ids:
    .hword  0x1E48, 0xC8
    .hword  0x2128, 0x3E
    .hword  0x2408, 0xD9
    .hword  0x26E8, 0xDA
    .hword  0, 0

@ r0 = Mix. r1 = id. Returns r0 = the selected pad's value. Clobbers r1-r3, r12.
.thumb_func
get_value:
    push    {r4, lr}
    sub     sp, #16
    add.w   r2, r0, #0x1000
    ldrh.w  r2, [r2, #(M_SEL - 0x1000)]
    ldr     r3, =REF_TAG
    str     r3, [sp]
    strh    r2, [sp, #4]
    movs    r3, #0
    str     r3, [sp, #8]
    mov     r2, r1
    mov     r1, sp
    add     r3, sp, #8
    ldr     r0, =APP
    call    GET_PARAM
    ldr     r0, [sp, #8]
    add     sp, #16
    pop     {r4, pc}

@ Bind the four value sliders to the knob params for the selected pad (stock 0x63 bound
@ two of them to Level/Pan). r0 = Mix.
.thumb_func
bind_knobs:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    adr     r5, slider_ids
1:
    ldrh    r6, [r5]
    cbz     r6, 2f
    ldrh    r1, [r5, #2]
    mov     r0, r4
    bl      get_value
    mov     r1, r0
    ldrh    r0, [r5, #2]
    add.w   r2, r4, r6
    movs    r3, #0
    call    SLIDER_BIND
    adds    r5, #4
    b       1b
2:
    mov     r0, r4
    bl      show_values
    pop     {r4, r5, r6, pc}

@ (fader offset, id) for each layout; the hidden faders are not refreshed.
.align 2
fader_ids_normal:
    .hword  F_CUT, 0xC8
    .hword  F_DEC, 0x3E
    .hword  F_SA,  0xD9
    .hword  F_SB,  0xDA
    .hword  0, 0
fader_ids_held:
    .hword  F_SA,  0x04
    .hword  F_SB,  0x62
    .hword  0, 0

@ Point every visible fader at the selected pad and its current value. r0 = Mix.
.thumb_func
show_values:
    push    {r3, r4, r5, r6, r7, lr}
    mov     r4, r0
    bl      mx_state
    ldr     r1, [r0, #MX_HELD]
    adr     r5, fader_ids_normal
    cmp     r1, #1
    bne     1f
    adr     r5, fader_ids_held
1:
    add.w   r7, r4, #0x1000
    ldrh.w  r7, [r7, #(M_SEL - 0x1000)]
2:
    ldrh    r6, [r5]
    cbz     r6, 3f
    add     r6, r4
    strh    r7, [r6, #F_PAD]
    ldrh    r1, [r5, #2]
    mov     r0, r4
    bl      get_value
    mov     r2, r0
    ldrh    r1, [r5, #2]
    mov     r0, r6
    call    FADER_SET
    adds    r5, #4
    b       2b
3:
    pop     {r3, r4, r5, r6, r7, pc}

@ Fader placement in GUI coordinates: (offset, x, y, w, h, hidden, label).
.align 2
place_normal:
    .hword  F_CUT, 1,   111, 30, 109, 0
    .word   s_cut
    .hword  F_DEC, 288, 111, 30, 109, 0
    .word   s_dec
    .hword  F_SA,  1,   1,   30, 109, 0
    .word   s_sa
    .hword  F_SB,  288, 1,   30, 109, 0
    .word   s_sb
    .hword  0, 0, 0, 0, 0, 0
    .word   0
place_held:
    .hword  F_CUT, 1,   111, 30, 109, 1
    .word   s_cut
    .hword  F_DEC, 288, 111, 30, 109, 1
    .word   s_dec
    .hword  F_SA,  1,   1,   30, 219, 0
    .word   s_vol
    .hword  F_SB,  288, 1,   30, 219, 0
    .word   s_pan
    .hword  0, 0, 0, 0, 0, 0
    .word   0

@ r0 = Mix, r1 = held (0/1). Places the faders, records the layout, refreshes values.
.thumb_func
apply_layout:
    push    {r3, r4, r5, r6, r7, lr}
    sub     sp, #16
    mov     r4, r0
    mov     r7, r1
    adr     r5, place_normal
    cbz     r1, 1f
    adr     r5, place_held
1:
    ldrh    r6, [r5]
    cbz     r6, 2f
    add     r6, r4
    ldrh    r0, [r5, #2]
    str     r0, [sp]
    ldrh    r0, [r5, #4]
    str     r0, [sp, #4]
    ldrh    r0, [r5, #6]
    str     r0, [sp, #8]
    ldrh    r0, [r5, #8]
    str     r0, [sp, #12]
    mov     r0, r6
    mov     r1, sp
    call    FADER_RECT
    ldrh    r0, [r5, #10]
    strb    r0, [r6, #W_HIDDEN]
    add.w   r0, r6, #F_LABEL
    ldr     r1, [r5, #12]
    call    SET_TEXT
    adds    r5, #16
    b       1b
2:
    bl      mx_state
    str     r7, [r0, #MX_HELD]
    mov     r0, r4
    bl      show_values
    add     sp, #16
    pop     {r3, r4, r5, r6, r7, pc}

@ Attach the four faders once per Mix widget and hide the empty frames behind them.
@ r0 = Mix. Returns r0 = 1 if it attached now.
.align 2
fader_offs:
    .hword  F_CUT, F_DEC, F_SA, F_SB, 0
frame_offs:
    .hword  0x29C8, 0x2A68, 0x2B08, 0x2BA8, 0

.thumb_func
attach:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldr     r1, =F_CUT + W_PARENT
    ldr     r1, [r4, r1]
    cmp     r1, r4
    beq     8f
    adr     r5, fader_offs
1:
    ldrh    r1, [r5], #2
    cbz     r1, 2f
    add     r1, r4
    mov     r0, r4
    call    ADD_CHILD
    b       1b
2:
    adr     r5, frame_offs
    movs    r6, #1
3:
    ldrh    r1, [r5], #2
    cbz     r1, 4f
    add     r1, r4
    strb    r6, [r1, #W_HIDDEN]
    b       3b
4:
    mov     r0, r4
    movs    r1, #0
    bl      apply_layout
    mov     r0, r4
    bl      bind_knobs
    movs    r0, #1
    pop     {r4, r5, r6, pc}
8:
    movs    r0, #0
    pop     {r4, r5, r6, pc}

@ ---- vtable +0: tick. MIX hold layout and the INFO momentary release.

.thumb_func
mix_tick:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r0, r4
    bl      attach
    mov     r6, r0
    mov     r0, r4
    mov     r1, r5
    call    MIX_TICK
    add.w   r0, r4, #0x1000
    ldrb.w  r0, [r0, #(M_MODE - 0x1000)]
    cbnz    r0, 2f
    bl      mx_state
    ldr     r1, =BTN_MIX
    ldrb    r1, [r1]
    cmp     r1, #0
    ite     eq
    moveq   r1, #1                  @ level 0 = pressed
    movne   r1, #0
    ldr     r2, [r0, #MX_HELD]
    cmp     r1, r2
    beq     1f
    mov     r0, r4
    bl      apply_layout
    movs    r6, #1
1:
    cbz     r6, 2f
    ldrb    r0, [r5]                @ full redraw: faders moved or appeared
    orr     r0, r0, #1
    strb    r0, [r5]
2:
    bl      mx_state
    ldr     r1, [r0, #MX_INFO]
    cbz     r1, 9f
    ldr     r2, =BTN_INFO
    ldrb    r2, [r2]
    cbnz    r2, 5f                  @ released
    cmp     r1, #1
    bne     9f
    ldr     r2, =UWTICK
    ldr     r2, [r2]
    ldr     r3, [r0, #MX_T0]
    subs    r2, r2, r3
    cmp     r2, #MX_HOLD_MS
    blo     9f
    movs    r1, #2
    str     r1, [r0, #MX_INFO]
    b       9f
5:
    movs    r2, #0
    str     r2, [r0, #MX_INFO]
    cmp     r1, #2
    bne     9f
    ldr     r0, =APP
    movs    r1, #0x2E
    movs    r2, #0
    movs    r3, #0
    call    SCREEN
9:
    pop     {r4, r5, r6, pc}

@ ---- App_HandleInput call sites (replace bl App_SetScreen; r0 = app, r1 = screen)

@ MIX button (0x080A32DE): stock picks 0x2F when already on Mix. Stay on Mix instead;
@ holding MIX there is the Vol/Pan + select layer.
.thumb_func
mix_btn_screen:
    cmp     r1, #0x2F
    bne     1f
    bx      lr
1:
    ldr     r12, =SCREEN
    bx      r12

@ INFO on Mix (0x080A30DC): open Mute and time the press for the momentary release.
.thumb_func
info_to_mute:
    push    {r0, r1, r2, r3, lr}
    bl      mx_state
    movs    r1, #1
    str     r1, [r0, #MX_INFO]
    ldr     r1, =UWTICK
    ldr     r1, [r1]
    str     r1, [r0, #MX_T0]
    pop     {r0, r1, r2, r3, lr}
    ldr     r12, =SCREEN
    bx      r12

.align 2
s_cut: .asciz "Cu"
s_dec: .asciz "De"
s_sa:  .asciz "A"
s_sb:  .asciz "B"
s_vol: .asciz "Vo"
s_pan: .asciz "Pa"
.align 2
.ltorg
