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
@   attached) become children and replace the empty frames. Two layers, same corners
@   (the GUI's y axis runs bottom-up, so logical y=1 is the physical bottom row):
@       Main:       Vol 0x04    | Cutoff 0xC8      MIX held:  Pan 0x62    | Send A 0xD9
@                   Decay 0x3E  | Pitch 0x5C                  Attack 0x3C | Send B 0xDA
@   Corner faders: top-left +0x2C48, top-right +0x2DA0, bottom-left +0x2CF4,
@   bottom-right +0x2E4C. A fader's own touch handler posts 0x6E {pad +0x38, id, value}.
@ - Knobs drive the four stock value-engine sliders, bound to the same corner's param, so
@   step, acceleration and App_SetParam stay stock. Logical encoder order is column-major:
@   0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right.
@ - Bipolar params (min = -max: Cutoff, Pitch, Pan) draw a white centre line with the bar
@   growing from the centre, and a drag within MX_SNAP_PX of the centre sticks at 0.
@ - Blip: touching a fader whose param sits at its rest position (rest_values) sets the
@   value under the finger as usual; letting go within MX_BLIP_MS puts the rest value
@   back. Longer touches keep the new value. Our faders get their own copy of the fader
@   vtable (fader_vt) so faders on other screens are untouched.
@ - INFO on Mix opens Mute. Released within MX_HOLD_MS it stays (toggle); held longer it
@   goes back to Mix on release (momentary).
@
@ UI task only (App_HandleInput, GUI tick/event/touch). RAM only. MIX and INFO send no
@ release event, so hold state is the debounced level byte in the button table (0 = pressed).

.equ MX_STATE,    0x38005200        @ past the Warp flags (0x38005000..0x38005110)
.equ MX_MAGIC,    0x5558494D        @ 'MIXU'
.equ MX_LAYER,    4                 @ layer shown: 0 main, 1 MIX held, 0xFF none
.equ MX_INFO,     8                 @ 0 idle, 1 INFO pressed on Mix, 2 momentary
.equ MX_T0,       12                @ uwTick when INFO opened Mute
.equ MX_BLIP,     16                @ fader touched at its rest value, or 0
.equ MX_BT0,      20                @ uwTick of that touch
.equ MX_HOLD_MS,  400
.equ MX_BLIP_MS,  300

.equ APP,         0x24020088
.equ UWTICK,      0x24015CE4
.equ BTN_MIX,     0x24009B36        @ button table entry 5 +0xE
.equ BTN_INFO,    0x24009BDE        @ button table entry 12 +0xE
.equ REF_TAG,     0x080EAF38
.equ EV_TAG,      0x080CFBA4

.equ MIX_TICK,    0x080B6601
.equ MIX_CHILD,   0x080B5A4D
.equ MIX_EVENT,   0x080B5C71
.equ ADD_CHILD,   0x080AEC45
.equ FADER_SET,   0x080C20D1        @ (fader, id, value)
.equ FADER_RECT,  0x080C1EE5        @ fader setRect (also places its label)
.equ FADER_DOWN,  0x080C1F19        @ stock touchDown (calls vtable +0x14 touchMove)
.equ FADER_UP,    0x080C1F2F        @ stock touchUp
.equ FADER_DRAW,  0x080C2135        @ stock draw
.equ FADER_VALUE, 0x080C1FB5        @ (fader, dy, h, id) -> value under the finger
.equ FADER_BAR,   0x080C2040 + 1    @ (fader, value): bar rect +0x44..+0x50
.equ PARAM_MIN,   0x0808E2D1
.equ PARAM_MAX,   0x0808E305
.equ FILL_RECT,   0x0808EA23        @ (rect*, colour, canvas)
.equ OUTLINE,     0x0808E995        @ (rect*, colour, canvas)
.equ DRAW_KIDS,   0x080AEAF9        @ (widget, ctx)
.equ MX_SNAP_PX,  4
.equ C_GREY,      1
.equ C_WHITE,     3
.equ C_CYAN,      5
.equ SLIDER_BIND, 0x080A7EE1        @ (id, value, slider, 0)
.equ SLIDER_STEP, 0x080B0D55        @ (slider, delta)
.equ GET_PARAM,   0x08099505        @ (app, ref, id, &out)
.equ SET_TEXT,    0x080C3B99        @ ui_draw_cstring(label, str)

.equ M_MODE,      0x1E40
.equ M_SEL,       0x1E08
.equ F_TL,        0x2C48
.equ F_TR,        0x2DA0
.equ F_BL,        0x2CF4
.equ F_BR,        0x2E4C
.equ S_TL,        0x1E48
.equ S_BL,        0x2128
.equ S_TR,        0x2408
.equ S_BR,        0x26E8
.equ W_HIDDEN,    0x30
.equ W_PARENT,    0x2C
.equ F_PAD,       0x38
.equ F_ID,        0x3E
.equ F_LABEL,     0x54

.global mix_tick
.global mix_child_event
.global mix_on_event
.global mix_btn_screen
.global info_to_mute
.global fader_vt
.global fader_down
.global fader_up
.global fader_draw
.global fader_move

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
    str     r1, [r0, #MX_LAYER]
    movs    r1, #0
    str     r1, [r0, #MX_INFO]
    str     r1, [r0, #MX_T0]
    str     r1, [r0, #MX_BLIP]
    str     r1, [r0, #MX_BT0]
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
    movs    r1, #1                  @ select: rebind the sliders too (stock bound Level/Pan)
    bl      show_layer
    b       5f
3:
    cmp     r5, #0x66
    beq     4f
    cmp     r5, #0x67
    bne     5f
4:
    mov     r0, r4
    movs    r1, #0
    bl      show_layer
5:
    pop     {r4, r5, r6, pc}
9:
    bx      lr

@ Logical encoder -> the value slider of the same corner.
.align 2
knob_slider:
    .hword  S_TL                    @ 0 top-left
    .hword  S_BL                    @ 1 bottom-left
    .hword  S_TR                    @ 2 top-right
    .hword  S_BR                    @ 3 bottom-right

@ Per layer and corner: (fader, slider, id, 0) + label.
.align 2
layer_main:
    .hword  F_TL, S_TL, 0x04, 0
    .word   s_vol
    .hword  F_BL, S_BL, 0x3E, 0
    .word   s_dec
    .hword  F_TR, S_TR, 0xC8, 0
    .word   s_cut
    .hword  F_BR, S_BR, 0x5C, 0
    .word   s_pit
    .hword  0, 0, 0, 0
    .word   0
layer_held:
    .hword  F_TL, S_TL, 0x62, 0
    .word   s_pan
    .hword  F_BL, S_BL, 0x3C, 0
    .word   s_att
    .hword  F_TR, S_TR, 0xD9, 0
    .word   s_sa
    .hword  F_BR, S_BR, 0xDA, 0
    .word   s_sb
    .hword  0, 0, 0, 0
    .word   0

@ Rest positions where a quick touch blips: (id, value). Pan is not listed.
.align 2
rest_values:
    .word   0x04, -96000            @ Vol at minimum
    .word   0xC8, 0                 @ Cutoff centre (filter off)
    .word   0x5C, 0                 @ Pitch unshifted
    .word   0x3C, 0                 @ Attack
    .word   0x3E, 0                 @ Decay
    .word   0xD9, 0                 @ Send A
    .word   0xDA, 0                 @ Send B
    .word   0xFFFF, 0

@ r0 = id. Returns r0 = rest value, r1 = 1 if the id has one. Clobbers r2, r3.
.thumb_func
rest_of:
    adr     r2, rest_values
1:
    ldr     r3, [r2]
    movw    r1, #0xFFFF
    cmp     r3, r1
    beq     2f
    cmp     r3, r0
    beq     3f
    adds    r2, #8
    b       1b
2:
    movs    r0, #0
    movs    r1, #0
    bx      lr
3:
    ldr     r0, [r2, #4]
    movs    r1, #1
    bx      lr

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

@ Show the current layer for the selected pad. r0 = Mix, r1 = 1 to also bind labels and
@ the knob sliders (layer change, pad select), 0 to refresh the fader values only.
.thumb_func
show_layer:
    push    {r4, r5, r6, r7, r8, lr}
    mov     r4, r0
    mov     r8, r1
    bl      mx_state
    ldr     r1, [r0, #MX_LAYER]
    adr     r5, layer_main
    cmp     r1, #1
    bne     1f
    adr     r5, layer_held
1:
    add.w   r7, r4, #0x1000
    ldrh.w  r7, [r7, #(M_SEL - 0x1000)]
2:
    ldrh    r6, [r5]
    cbz     r6, 4f
    add     r6, r4
    strh    r7, [r6, #F_PAD]
    ldrh    r1, [r5, #4]
    mov     r0, r4
    bl      get_value
    push    {r0, r1}
    mov     r2, r0
    ldrh    r1, [r5, #4]
    mov     r0, r6
    call    FADER_SET
    pop     {r0, r1}
    cmp     r8, #0
    beq     3f
    mov     r1, r0
    ldrh    r0, [r5, #4]
    ldrh    r2, [r5, #2]
    add     r2, r4
    movs    r3, #0
    call    SLIDER_BIND
    add.w   r0, r6, #F_LABEL
    ldr     r1, [r5, #8]
    call    SET_TEXT
3:
    adds    r5, #12
    b       2b
4:
    pop     {r4, r5, r6, r7, r8, pc}

@ Corner rects in GUI coordinates (fader, x, y, w, h).
.align 2
corner_rects:
    .hword  F_TL, 1,   111, 30, 109
    .hword  F_TR, 288, 111, 30, 109
    .hword  F_BL, 1,   1,   30, 109
    .hword  F_BR, 288, 1,   30, 109
    .hword  0, 0, 0, 0, 0
frame_offs:
    .hword  0x29C8, 0x2A68, 0x2B08, 0x2BA8, 0

@ Attach the four faders once per Mix widget, give them our vtable and corners, and hide
@ the empty frames behind them. r0 = Mix. Returns r0 = 1 if it attached now.
.thumb_func
attach:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #20
    mov     r4, r0
    ldr     r1, =F_TL + W_PARENT
    ldr     r1, [r4, r1]
    cmp     r1, r4
    beq     8f
    adr     r5, corner_rects
1:
    ldrh    r6, [r5]
    cbz     r6, 2f
    add     r6, r4
    ldr     r0, =fader_vt
    str     r0, [r6]
    mov     r0, r4
    mov     r1, r6
    call    ADD_CHILD
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
    movs    r0, #0
    strb    r0, [r6, #W_HIDDEN]
    adds    r5, #10
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
    bl      mx_state
    movs    r1, #0
    str     r1, [r0, #MX_LAYER]
    mov     r0, r4
    movs    r1, #1
    bl      show_layer
    movs    r0, #1
    b       9f
8:
    movs    r0, #0
9:
    add     sp, #20
    pop     {r4, r5, r6, r7, pc}

@ ---- vtable +0: tick. MIX layer and the INFO momentary release.

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
    ldr     r2, [r0, #MX_LAYER]
    cmp     r1, r2
    beq     1f
    str     r1, [r0, #MX_LAYER]
    mov     r0, r4
    movs    r1, #1
    bl      show_layer
    movs    r6, #1
1:
    cbz     r6, 2f
    ldrb    r0, [r5]                @ full redraw: faders appeared or changed layer
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

@ ---- fader touch (our faders only, via fader_vt)

@ touchDown(fader, point): remember a touch that starts at the rest value, then stock.
.thumb_func
fader_down:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    ldrh    r0, [r4, #F_ID]
    bl      rest_of
    mov     r6, r0                  @ rest value
    cbz     r1, 1f
    ldrh    r1, [r4, #F_ID]
    ldr     r0, [r4, #W_PARENT]
    bl      get_value
    cmp     r0, r6
    bne     1f
    bl      mx_state
    str     r4, [r0, #MX_BLIP]
    ldr     r1, =UWTICK
    ldr     r1, [r1]
    str     r1, [r0, #MX_BT0]
    b       2f
1:
    bl      mx_state
    movs    r1, #0
    str     r1, [r0, #MX_BLIP]
2:
    mov     r0, r4
    mov     r1, r5
    call    FADER_DOWN
    pop     {r4, r5, r6, pc}

@ touchUp(fader): stock, then a quick touch from the rest value puts it back.
.thumb_func
fader_up:
    push    {r4, r5, lr}
    sub     sp, #28
    mov     r4, r0
    call    FADER_UP
    bl      mx_state
    ldr     r1, [r0, #MX_BLIP]
    cmp     r1, r4
    bne     9f
    movs    r1, #0
    str     r1, [r0, #MX_BLIP]
    ldr     r2, =UWTICK
    ldr     r2, [r2]
    ldr     r3, [r0, #MX_BT0]
    subs    r2, r2, r3
    cmp     r2, #MX_BLIP_MS
    bhs     9f
    ldrh    r0, [r4, #F_ID]
    bl      rest_of
    mov     r5, r0
    movs    r0, #0x6E               @ same event the fader's touchMove posts
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    ldrh    r0, [r4, #F_PAD]
    strh    r0, [sp, #8]
    ldrh    r0, [r4, #F_ID]
    str     r0, [sp, #12]
    str     r5, [sp, #16]
    movs    r0, #0
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, sp
    call    UI_POST_UP
    mov     r0, r4
    ldrh    r1, [r4, #F_ID]
    mov     r2, r5
    call    FADER_SET
9:
    add     sp, #28
    pop     {r4, r5, pc}

@ r0 = fader. Returns r0 = 1 if its param is bipolar: min < 0 and min = -max (Cutoff,
@ Pitch, Pan; not Vol, which is -96 dB..+12 dB). Clobbers r1-r3, r12.
.thumb_func
is_bipolar:
    push    {r4, r5, r6, lr}
    ldrh    r5, [r0, #F_ID]
    mov     r0, r5
    call    PARAM_MIN
    mov     r4, r0
    mov     r0, r5
    call    PARAM_MAX
    adds    r0, r0, r4
    bne     1f
    cmp     r4, #0
    bge     1f
    movs    r0, #1
    pop     {r4, r5, r6, pc}
1:
    movs    r0, #0
    pop     {r4, r5, r6, pc}

@ draw(fader, ctx): stock look for unipolar params; bipolar ones get a centre line and a
@ bar from the centre to the value. ctx[0] = full redraw, ctx+4 = canvas.
.thumb_func
fader_draw:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #20
    mov     r4, r0
    mov     r5, r1
    ldrb    r0, [r5]
    cbz     r0, 8f
    mov     r0, r4
    bl      is_bipolar
    cbz     r0, 8f
    ldr     r6, [r5, #4]            @ canvas
    add.w   r0, r4, #4
    movs    r1, #C_GREY
    mov     r2, r6
    call    FILL_RECT
    add.w   r0, r4, #4
    movs    r1, #C_CYAN
    mov     r2, r6
    call    OUTLINE
    ldr     r0, [r4, #0x10]         @ h
    lsrs    r7, r0, #1              @ centre offset c
    ldr     r0, [r4, #0x50]         @ bar height from the bottom edge
    ldr     r1, [r4, #4]
    str     r1, [sp]                @ x
    ldr     r1, [r4, #0xC]
    str     r1, [sp, #8]            @ w
    ldr     r1, [r4, #8]            @ y
    cmp     r0, r7
    blt     1f
    add     r2, r1, r7              @ value above centre: y+c .. y+bar
    str     r2, [sp, #4]
    subs    r0, r0, r7
    b       2f
1:
    add     r2, r1, r0              @ below centre: y+bar .. y+c
    str     r2, [sp, #4]
    subs    r0, r7, r0
2:
    str     r0, [sp, #12]
    cbz     r0, 3f
    mov     r0, sp
    movs    r1, #C_CYAN
    mov     r2, r6
    call    FILL_RECT
3:
    ldr     r1, [r4, #8]            @ centre line, 1 px, full width
    add     r1, r7
    str     r1, [sp, #4]
    movs    r1, #1
    str     r1, [sp, #12]
    mov     r0, sp
    movs    r1, #C_WHITE
    mov     r2, r6
    call    FILL_RECT
    mov     r0, r4
    mov     r1, r5
    call    DRAW_KIDS
    add     sp, #20
    pop     {r4, r5, r6, r7, pc}
8:
    mov     r0, r4
    mov     r1, r5
    add     sp, #20
    pop     {r4, r5, r6, r7, lr}
    ldr     r12, =FADER_DRAW
    bx      r12

@ touchMove(fader, point): stock value mapping, plus a centre snap for bipolar params.
.thumb_func
fader_move:
    push    {r4, r5, r6, lr}
    sub     sp, #24
    mov     r4, r0
    ldr     r5, [r1, #4]            @ touch y
    ldr     r0, [r4, #8]
    subs    r5, r5, r0              @ dy from the bottom edge
    mov     r0, r4
    mov     r1, r5
    ldr     r2, [r4, #0x10]
    ldrh    r3, [r4, #F_ID]
    call    FADER_VALUE
    mov     r6, r0
    mov     r0, r4
    bl      is_bipolar
    cbz     r0, 1f
    ldr     r0, [r4, #0x10]
    lsrs    r0, r0, #1
    subs    r0, r5, r0              @ dy - h/2
    it      mi
    negmi   r0, r0
    cmp     r0, #MX_SNAP_PX
    it      ls
    movls   r6, #0
1:
    mov     r0, r4
    mov     r1, r6
    call    FADER_BAR
    movs    r0, #0x6E               @ what stock touchMove posts
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    ldrh    r0, [r4, #F_PAD]
    strh    r0, [sp, #8]
    ldrh    r0, [r4, #F_ID]
    str     r0, [sp, #12]
    str     r6, [sp, #16]
    movs    r0, #0
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, sp
    call    UI_POST_UP
    movs    r0, #1
    strb    r0, [r4, #0x40]         @ redraw
    add     sp, #24
    pop     {r4, r5, r6, pc}

@ Copy of the stock fader vtable 0x080F16D8 with touchDown/touchUp wrapped.
.align 2
fader_vt:
    .word   0x080C1EC1              @ +0x00 tick
    .word   fader_draw              @ +0x04 draw (centre detent for bipolar params)
    .word   0x080AEB1D
    .word   0x080AEBAB              @ +0x0C contains
    .word   fader_down              @ +0x10 touchDown (Thumb bit from the symbol)
    .word   fader_move              @ +0x14 touchMove (snap to centre)
    .word   fader_up                @ +0x18 touchUp
    .word   0x080C1EE1              @ +0x1C acceptsTouch
    .word   0x080C1EE5              @ +0x20 setRect
    .word   0x080A3317
    .word   0x080AEB5F              @ +0x28 hitTest
    .word   0x080A331B
    .word   0x080AEC05              @ +0x30 onChildEvent
    .word   0x080C1F17              @ +0x34 onEvent
    .word   0
    .word   0

@ ---- App_HandleInput call sites (replace bl App_SetScreen; r0 = app, r1 = screen)

@ MIX button (0x080A32DE): stock picks 0x2F when already on Mix. Stay on Mix instead;
@ holding MIX there is the second layer + select-without-play.
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
s_vol: .asciz "Vo"
s_dec: .asciz "De"
s_cut: .asciz "Cu"
s_pit: .asciz "Pi"
s_pan: .asciz "Pa"
s_att: .asciz "At"
s_sa:  .asciz "A"
s_sb:  .asciz "B"
.align 2
.ltorg
