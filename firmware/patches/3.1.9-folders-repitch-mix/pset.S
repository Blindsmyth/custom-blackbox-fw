@ Preset list opens on its first row (the first folder) instead of the loaded preset.
@ docs/firmware/README.md "Presets".
@
@ Preset screen onEvent 0x080BDE60: event 0x68 selects the loaded preset by name, and
@ event 0x149 (list rebuilt) selects the pending row name (+0x1CDF, kept around a load) or
@ else the loaded preset, falling back to row 0 when the name isn't listed. Both look the
@ name up with FUN_08097AC8(sessionmgr, buf) -> 0 when there is none. With preset folders
@ the browse path starts in the loaded preset's folder, so the list opened on that preset.
@ Both calls now report "no name": 0x68 leaves the selection alone and 0x149 picks row 0.
@ The loaded preset's own highlight (preset_screen_draw) keeps calling FUN_08097AC8.

.global pset_no_current

@ bl sites 0x080BDE96 / 0x080BDFEE (was bl FUN_08097AC8).
.thumb_func
pset_no_current:
    movs    r0, #0
    bx      lr

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
