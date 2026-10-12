@ Mix screen pads: mute / solo instead of play. docs/firmware/README.md "Mix".
@
@ The Mix view always runs in the stock mute mode (+0x1E40 = 1; set-mode 0x080B5E44 also
@ switches each cell's display, cell+0x18A), so pads draw green (playing) / red (muted) and
@ our four faders stay on top. Touches on the view (vtable +0x10 / +0x14):
@ - MIX held: tap selects the pad (posts 0x63 up, as stock select-only).
@ - Mute (INFO cycles Mute -> Solo -> Mute): stock mute touch, drag paints mutes.
@ - Solo: tap toggles the pad in the solo set; while the set is non-empty every other pad
@   is muted through the stock mute path (msg 0x44 {pad, unmuted} up the widget chain +
@   cell state 0x080A4F10). Emptying the set restores the mutes captured when the first pad
@   was soloed. A hand edit in Mute mode drops the set (keeps what was set by hand).
@   Soloed pads draw yellow; in Solo mode every pad gets a yellow outline.
@ Solo logic after j3threejay/blackbox-mod src/solo.c (MIT), addresses verified on 3.1.9.

.equ MX_PMODE,    92                    @ MX_STATE: 0 Mute, 1 Solo
.equ MX_VIEW,     96                    @ the Mix view (from set-mode)
.equ MX_SMASK,    100                   @ soloed cells, bit = cell index
.equ MX_SSAVED,   104                   @ muted cells when the solo set became non-empty
.equ PM_COUNT,    2

.equ V_CELLS,     0x3AC                 @ 16 cells, 0x1A0 each
.equ CELL_SIZE,   0x1A0
.equ CELL_PAD,    0x38                  @ u16 pad id: col | row << 4
.equ CELL_DIRTY,  0x17C
.equ CELL_UNMUTED, 0x189
.equ C_SOLO_FILL, 0x1C                  @ palette: dark yellow
.equ C_SOLO_EDGE, 0x14                  @ palette: yellow

.equ SET_MODE,    0x080B5E45            @ (view, mute_mode)
.equ HIT_CELL,    0x080B5E79            @ (view, point, &cell) -> nonzero on a hit
.equ MUTE_DOWN,   0x080B5F45            @ stock view touchDown (mute mode)
.equ MUTE_MOVE,   0x080B5EBD            @ stock view touchMove (mute mode)
.equ CELL_SET,    0x080A4F11            @ (cell, unmuted)
.equ POST_UP,     0x080AED15            @ (widget, msg): to the parent's onChildEvent
.equ MSG_MUTE,    0x44

.global mix_set_mode
.global mix_touch_down
.global mix_touch_move
.global mix_info
.global mix_mute_fill

@ bl site 0x0808BC40 (GUI shows Mix or Mute): the view always shows mute colours.
.thumb_func
mix_set_mode:
    push    {r0, r1, r2, lr}
    bl      mx_state
    ldr     r1, [sp]
    str     r1, [r0, #MX_VIEW]
    pop     {r0, r1, r2, lr}
    movs    r1, #1
    ldr     r12, =SET_MODE
    bx      r12

@ r0 = view, r1 = index -> r0 = cell.
.thumb_func
mm_cell:
    add     r0, r0, #V_CELLS
    mov.w   r2, #CELL_SIZE
    mla     r0, r1, r2, r0
    bx      lr

@ r0 = view: every cell repaints.
.thumb_func
mm_dirty:
    add     r0, r0, #V_CELLS
    movs    r1, #16
    movs    r2, #1
1:
    strb.w  r2, [r0, #CELL_DIRTY]
    add     r0, r0, #CELL_SIZE
    subs    r1, #1
    bne     1b
    bx      lr

@ mm_mute(view, cell, unmuted): the stock mute path for one pad.
.thumb_func
mm_mute:
    push    {r4, r5, r6, lr}
    sub     sp, #24
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    movs    r0, #MSG_MUTE
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    ldrh    r0, [r5, #CELL_PAD]
    strh    r0, [sp, #8]
    str     r6, [sp, #12]
    movs    r0, #0
    str     r0, [sp, #16]
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, sp
    call    POST_UP
    mov     r0, r5
    mov     r1, r6
    call    CELL_SET
    add     sp, #24
    pop     {r4, r5, r6, pc}

@ mm_apply(view): solo set non-empty -> soloed pads play, others muted; empty -> the saved
@ mutes come back. Cells whose column > 3 are skipped (as stock mute).
.thumb_func
mm_apply:
    push    {r4, r5, r6, r7, r8, lr}
    mov     r4, r0
    bl      mx_state
    ldr     r6, [r0, #MX_SMASK]
    ldr     r7, [r0, #MX_SSAVED]
    movs    r5, #0
1:
    mov     r0, r4
    mov     r1, r5
    bl      mm_cell
    mov     r8, r0
    ldrh    r0, [r8, #CELL_PAD]
    and     r0, r0, #0xF
    cmp     r0, #3
    bhi     3f
    cbz     r6, 2f
    lsr     r2, r6, r5
    and     r2, r2, #1              @ soloed: plays
    b       21f
2:
    lsr     r2, r7, r5
    and     r2, r2, #1
    eor     r2, r2, #1              @ empty set: saved mutes back
21:
    ldrb.w  r0, [r8, #CELL_UNMUTED]
    cmp     r0, r2
    beq     3f
    mov     r0, r4
    mov     r1, r8
    bl      mm_mute
3:
    adds    r5, #1
    cmp     r5, #16
    blo     1b
    mov     r0, r4
    bl      mm_dirty
    pop     {r4, r5, r6, r7, r8, pc}

@ mm_toggle(view, cell): flip the pad in the solo set.
.thumb_func
mm_toggle:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #4
    mov     r4, r0
    mov     r5, r1
    ldrh    r0, [r5, #CELL_PAD]
    and     r0, r0, #0xF
    cmp     r0, #3
    bhi     9f
    add     r0, r4, #V_CELLS
    subs    r0, r5, r0
    blt     9f
    mov.w   r1, #CELL_SIZE
    udiv    r6, r0, r1              @ index
    cmp     r6, #16
    bhs     9f
    bl      mx_state
    mov     r7, r0
    ldr     r0, [r7, #MX_SMASK]
    cbnz    r0, 2f
    movs    r2, #0                  @ first solo: remember the hand-set mutes
    movs    r3, #0
1:
    mov     r0, r4
    mov     r1, r3
    push    {r2, r3}
    bl      mm_cell
    pop     {r2, r3}
    ldrb.w  r0, [r0, #CELL_UNMUTED]
    cbnz    r0, 11f
    movs    r1, #1
    lsls    r1, r3
    orrs    r2, r1
11:
    adds    r3, #1
    cmp     r3, #16
    blo     1b
    str     r2, [r7, #MX_SSAVED]
2:
    ldr     r0, [r7, #MX_SMASK]
    movs    r1, #1
    lsls    r1, r6
    eors    r0, r1
    str     r0, [r7, #MX_SMASK]
    mov     r0, r4
    bl      mm_apply
9:
    add     sp, #4
    pop     {r4, r5, r6, r7, pc}

@ vtable +0x10 touchDown(view, point, arg).
.thumb_func
mix_touch_down:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #28
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    bl      mx_state
    mov     r7, r0
    ldr     r0, [r7, #MX_LAYER]
    cmp     r0, #1
    beq     1f                      @ MIX held: select
    ldr     r0, [r7, #MX_PMODE]
    cmp     r0, #1
    beq     2f                      @ Solo
    movs    r0, #0                  @ Mute: a hand edit drops the solo set
    str     r0, [r7, #MX_SMASK]
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    call    MUTE_DOWN
    b       9f
1:
    movs    r0, #0
    str     r0, [sp, #24]
    mov     r0, r4
    mov     r1, r5
    add     r2, sp, #24
    call    HIT_CELL
    cbz     r0, 9f
    ldr     r1, [sp, #24]
    cbz     r1, 9f
    movs    r0, #0x63               @ select, no trigger
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    ldrh    r0, [r1, #CELL_PAD]
    strh    r0, [sp, #8]
    movs    r0, #0
    str     r0, [sp, #12]
    str     r0, [sp, #16]
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, sp
    call    POST_UP
    b       9f
2:
    movs    r0, #0
    str     r0, [sp, #24]
    mov     r0, r4
    mov     r1, r5
    add     r2, sp, #24
    call    HIT_CELL
    cbz     r0, 9f
    ldr     r1, [sp, #24]
    cbz     r1, 9f
    mov     r0, r4
    bl      mm_toggle
9:
    add     sp, #28
    pop     {r4, r5, r6, r7, pc}

@ vtable +0x14 touchMove: only Mute mode paints across pads.
.thumb_func
mix_touch_move:
    push    {r0, r1, r2, lr}
    bl      mx_state
    ldr     r1, [r0, #MX_LAYER]
    cmp     r1, #1
    beq     8f
    ldr     r1, [r0, #MX_PMODE]
    cbnz    r1, 8f
    pop     {r0, r1, r2, lr}
    ldr     r12, =MUTE_MOVE
    bx      r12
8:
    pop     {r0, r1, r2, pc}

@ bl site 0x080A30DC (INFO on Mix, was App_SetScreen(Mute)): next pad mode.
.thumb_func
mix_info:
    push    {r4, lr}
    bl      mx_state
    mov     r4, r0
    ldr     r1, [r4, #MX_PMODE]
    adds    r1, #1
    cmp     r1, #PM_COUNT
    it      hs
    movhs   r1, #0
    str     r1, [r4, #MX_PMODE]
    ldr     r0, [r4, #MX_VIEW]
    cbz     r0, 1f
    bl      mm_dirty
1:
    movs    r0, #1
    str     r0, [r4, #MX_REDRAW]
    pop     {r4, pc}

@ bl sites 0x080A43E0 / 0x080A43F8 (cell mute-mode fill, rect = cell + 4): Solo colours.
.thumb_func
mix_mute_fill:
    push    {r4, r5, r6, r7, r8, lr}
    sub     sp, #16
    mov     r4, r0                  @ rect
    mov     r5, r1                  @ colour
    mov     r6, r2                  @ canvas
    mov.w   r8, #-1                 @ cell index in Solo mode, else -1
    bl      mx_state
    mov     r7, r0
    ldr     r0, [r7, #MX_PMODE]
    cmp     r0, #1
    bne     5f
    ldr     r0, [r7, #MX_VIEW]
    cbz     r0, 5f
    add     r0, r0, #(V_CELLS + 4)
    subs    r0, r4, r0
    blt     5f
    mov.w   r1, #CELL_SIZE
    udiv    r2, r0, r1
    mls     r3, r2, r1, r0
    cbnz    r3, 5f
    cmp     r2, #16
    bhs     5f
    mov     r8, r2
    ldr     r0, [r7, #MX_SMASK]
    lsr     r0, r0, r8
    tst     r0, #1
    beq     5f
    ldrb.w  r0, [r4, #(CELL_UNMUTED - 4)]
    cbz     r0, 5f                  @ soloed and still playing: yellow
    movs    r5, #C_SOLO_FILL
5:
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    call    FILL_RECT
    cmp     r8, #0
    blt     9f
    mov     r0, r4                  @ Solo mode: yellow outline, two pixels
    movs    r1, #C_SOLO_EDGE
    mov     r2, r6
    call    OUTLINE
    ldm     r4, {r0, r1, r2, r3}
    adds    r0, #1
    adds    r1, #1
    subs    r2, #2
    subs    r3, #2
    stm     sp, {r0, r1, r2, r3}
    mov     r0, sp
    movs    r1, #C_SOLO_EDGE
    mov     r2, r6
    call    OUTLINE
9:
    add     sp, #16
    pop     {r4, r5, r6, r7, r8, pc}

.align 2
.ltorg
