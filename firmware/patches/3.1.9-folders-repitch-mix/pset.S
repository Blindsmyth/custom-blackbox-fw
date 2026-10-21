@ Preset list selection: the loaded preset when it is in the list, otherwise the first row.
@ docs/firmware/README.md "Presets screen".
@
@ Preset screen onEvent 0x080BDE60: event 0x68 (screen shown) selects the loaded preset by
@ name with FUN_080BD8C0(screen, name), which changes nothing when the name isn't listed;
@ the index then stays at the ctor's -1 (+0x614), which showed as the bottom row once
@ folders put other rows in the list. Event 0x149 (list rebuilt) selects the pending row
@ (+0x1CDF, kept around a load), else the loaded preset, else row 0 itself.
@ pset_select wraps both FUN_080BD8C0 calls: not found -> row 0.

.equ SEL_NAME,    0x080BD8C1            @ FUN_080BD8C0(screen, name) -> found
.equ LIST_INDEX,  0x080B074D            @ FUN_080B074C(slider, index)
.equ PSET_DRAW,   0x080BD7D5            @ preset_screen_draw(screen, index, force)

.global pset_select

@ bl sites 0x080BDEA4 (event 0x68) / 0x080BDFF8 (event 0x149).
.thumb_func
pset_select:
    push    {r4, lr}
    mov     r4, r0
    call    SEL_NAME
    cbnz    r0, 9f
    add     r0, r4, #0x618          @ not in this folder: first row
    movs    r1, #0
    call    LIST_INDEX
    mov     r0, r4
    movs    r1, #0
    movs    r2, #1
    call    PSET_DRAW
    movs    r0, #0
9:
    pop     {r4, pc}

@ ---- ^ button (like the sample browser's, FUN_080B2DF0)
@ The preset screen's top panel (screen+0x34, a slot row filled from the right) holds Load
@ (+0x1B38, id 0x18), Save (+0x1A98, 0x20), File (+0x19F8, 0x127) and, leftmost, a 38 x 38
@ button at +0x630 with id 0x7F: a tap posts key 0x7F, which repitch/cave.S hook_back_evt
@ turns into folder_back. Stock leaves it unlabelled. It shows "^" below the top level.
@ The ".." list row is gone (cave.S hook_dotdot, MIX_EXT); BACK and ^ go up instead.

.equ PSET_EVENT,  0x080BDE61            @ preset screen onEvent (vtable 0x080F14CC)
.equ PS_UP,       0x630                 @ the id-0x7F button
.equ LBL_TEXT,    0x080A3DED            @ FUN_080A3DEC(button, text): set label, redraw

.global pset_on_event

@ vtable +0x34: stock, then keep the ^ label in step with the browse path.
.thumb_func
pset_on_event:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    call    PSET_EVENT
    mov     r5, r0
    bl      ensure
    add     r0, r0, #S_BROWSE
    bl      is_top
    eor     r6, r0, #1              @ 1: show ^
    add     r0, r4, #PS_UP
    ldrb    r1, [r0, #(0x34 + 0x31)] @ label text, first character
    cmp     r1, #0x5E
    ite     eq
    moveq   r1, #1
    movne   r1, #0
    cmp     r1, r6
    beq     9f
    adr     r1, s_up
    cmp     r6, #0
    it      eq
    addeq   r1, #1                  @ ""
    call    LBL_TEXT
9:
    mov     r0, r5
    pop     {r4, r5, r6, pc}

s_up: .asciz "^"
.align 2
.ltorg
