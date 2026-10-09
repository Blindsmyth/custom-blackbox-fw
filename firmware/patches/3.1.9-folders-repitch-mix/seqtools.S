@ Batch 1: Seq screen length tools.
@ - Seq onEvent wrapper (vtable 0x080F0E90 +0x34). Encoders 0/1 no longer navigate
@   (Batch 0). Encoder 2 (top right) doubles (turn right) or halves (turn left) the current
@   layer of the selected sequence, at most once per SEQ_LEN_MS. Encoder 3 stays stock.
@ - Encoder 3 no longer swaps the side panel. While SEQS is held the layer panel (A-D,
@   Seq+0x4010) shows instead of the OFF/UNDO/CLR panel (Seq+0x3D5C); tap a layer there.
@   Stock swaps them by their hidden bytes (+0x30), so the Seq tick wrapper does the same.
@ - The unlabelled button above UNDO (Seq+0x3F70) shows the length in bars:
@   bars = 0x86 notestepcount x step length (table 0x080ECA6C, beats) / 4.
@ Double is stock FUN_0809C5B4. Halve mirrors it: undo snapshot, copy the layer's events,
@ drop the ones at or past the new end, set 0x86 = n / 2, push the layer, refresh.

.equ SEQ_EVENT,   0x080B5285
.equ SEQ_KEY,     0x1A90                @ selected sequence pad (byte)
.equ SEQ_BARS,    0x3F70                @ button above UNDO
.equ SEQ_LEN_MS,  250

.equ SEQ_OBJ,     0x08097CE9            @ (app, ref, layer) -> sequence object
.equ P_GET,       0x08093E9D            @ (obj, id)
.equ P_SET,       0x08093ECD            @ (obj, id, value)
.equ SEQ_DOUBLE,  0x0809C5B5            @ (app, event{+8 key})
.equ SEQ_SNAP,    0x08098255            @ undo snapshot (app, ref)
.equ SEQ_PUSH,    0x080985A1            @ (app, ref, layer)
.equ SONG_RELEN,  0x0809C2C9            @ (app)
.equ OBJ_INIT,    0x08094A1D            @ (obj, &type)
.equ OBJ_COPY,    0x08094BB3            @ (dst, src)
.equ EVL_CLEAR,   0x08063CC1            @ (list)
.equ EVL_GET,     0x08063D09            @ (list, index, &event) -> 0 at end
.equ EVL_DEL,     0x08063C09            @ (list, handle)
.equ UI_POST,     0x080B5759            @ ui_post_event(queue, event)
.equ SNPRINTF,    0x080C89F5
.equ BTN_TEXT,    0x080A3DED            @ (button, str)
.equ STEP_TABLE,  0x080ECA6C            @ float beats per step, by 0x85
.equ APP_SCRATCH, 0xE920                @ app scratch sequence object, list at +0x18

.equ MX_SEQ_T,    24                    @ MX_STATE: uwTick of the last length change
.equ MX_SEQ_PANEL, 40                   @ MX_STATE: 1 while the layer panel is shown
.equ SEQ_TICK,    0x080B55F5
.equ SEQ_PANEL_A, 0x3D5C
.equ SEQ_PANEL_B, 0x4010
.equ BTN_SEQS_LV, 0x24009AEE

.global seq_on_event
.global seq_tick
.global seq_halve
.global seq_bar_label

.thumb_func
seq_on_event:
    ldrh    r2, [r1]
    cmp     r2, #0x32
    bne     5f
    ldrh    r2, [r1, #0xC]
    cmp     r2, #2
    bne     9f                      @ 0, 1: navigation; 3: panel swap. All off.
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldrsh.w r5, [r1, #0x10]
    cbz     r5, 4f
    bl      mx_state
    ldr     r1, =UWTICK
    ldr     r1, [r1]
    ldr     r2, [r0, #MX_SEQ_T]
    subs    r2, r1, r2
    cmp     r2, #SEQ_LEN_MS
    blo     4f
    str     r1, [r0, #MX_SEQ_T]
    add.w   r1, r4, #0x1000
    ldrb.w  r1, [r1, #(SEQ_KEY - 0x1000)]
    ldr     r0, =APP
    cmp     r5, #0
    blt     2f
    sub     sp, #24                 @ stock Double reads the key at event+8
    strh    r1, [sp, #8]
    mov     r1, sp
    call    SEQ_DOUBLE
    add     sp, #24
    b       3f
2:
    bl      seq_halve
3:
    mov     r0, r4
    bl      seq_bar_label
4:
    pop     {r4, r5, r6, pc}
5:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r2
    call    SEQ_EVENT
    cmp     r5, #0x63
    beq     6f
    cmp     r5, #0x66
    beq     6f
    cmp     r5, #0x67
    bne     7f
6:
    mov     r0, r4
    bl      seq_bar_label
7:
    pop     {r4, r5, r6, pc}
9:
    bx      lr

@ tick(seq, flags): stock, then show the layer panel while SEQS is held.
.thumb_func
seq_tick:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    call    SEQ_TICK
    ldr     r1, =BTN_SEQS_LV
    ldrb    r1, [r1]
    cmp     r1, #0
    ite     eq
    moveq   r6, #1                  @ held
    movne   r6, #0
    bl      mx_state
    ldr     r1, [r0, #MX_SEQ_PANEL]
    cmp     r1, r6
    beq     9f
    str     r6, [r0, #MX_SEQ_PANEL]
    movw    r0, #SEQ_PANEL_A
    add     r0, r4
    strb    r6, [r0, #0x30]         @ OFF/UNDO/CLR hidden while held
    eor     r1, r6, #1
    movw    r0, #SEQ_PANEL_B
    add     r0, r4
    strb    r1, [r0, #0x30]         @ layers shown while held
    movs    r0, #1
    strb    r0, [r5]
    strb    r0, [r5, #1]
9:
    pop     {r4, r5, r6, pc}

@ r0 = app, r1 = sequence pad byte. Sets r0 = layer object, r1 = layer, r2 = base object,
@ and fills ref at [r3] (8 bytes). Clobbers r12.
.thumb_func
seq_layer:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #4
    mov     r4, r0
    mov     r7, r3
    ldr     r2, =REF_TAG
    str     r2, [r7]
    orr     r1, r1, #0x100
    strh    r1, [r7, #4]
    mov     r1, r7
    movs    r2, #0
    call    SEQ_OBJ
    mov     r5, r0                  @ base (layer 0) holds the active layer
    movs    r1, #0xC0
    call    P_GET
    uxtb    r6, r0
    mov     r0, r4
    mov     r1, r7
    mov     r2, r6
    call    SEQ_OBJ
    mov     r1, r6
    mov     r2, r5
    add     sp, #4
    pop     {r4, r5, r6, r7, pc}

@ r0 = layer object. Returns r0 = ticks per step (step length x 960, rounded). Clobbers r1-r3.
.thumb_func
step_ticks:
    push    {r4, lr}
    movs    r1, #0x85
    call    P_GET
    cmp     r0, #15
    it      hs
    movhs   r0, #10                 @ out of range: 1/16
    ldr     r1, =STEP_TABLE
    add.w   r1, r1, r0, lsl #2
    vldr    s0, [r1]
    vldr    s1, ticks_per_beat
    vmul.f32 s0, s0, s1
    vmov.f32 s1, #0.5
    vadd.f32 s0, s0, s1
    vcvt.s32.f32 s0, s0
    vmov    r0, s0
    pop     {r4, pc}

.align 2
ticks_per_beat: .float 960.0
tenths_per_beat: .float 2.5         @ bars x 10 per beat (4 beats per bar)

@ seq_halve(app, pad byte): halve the current layer of that sequence.
.thumb_func
seq_halve:
    push    {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    sub     sp, #44
    mov     r4, r0                  @ app
    mov     r8, r1                  @ pad byte
    add     r3, sp, #32             @ ref
    bl      seq_layer
    mov     r5, r0                  @ layer object
    mov     r9, r1                  @ layer
    mov     r0, r2
    movs    r1, #0x88               @ step mode keeps positions as step x 960
    call    P_GET
    mov     r10, r0
    mov     r0, r5
    movs    r1, #0x86
    call    P_GET
    cmp     r0, #2
    blt     9f
    lsrs    r6, r0, #1              @ new step count
    mov.w   r7, #960
    cmp     r10, #0
    bne     1f
    mov     r0, r5
    bl      step_ticks
    mov     r7, r0
1:
    mul     r7, r6, r7              @ first tick past the new end
    mov     r0, r4
    add     r1, sp, #32
    call    SEQ_SNAP
    add.w   r11, r4, #0xE000
    add.w   r11, r11, #(APP_SCRATCH - 0xE000)
    movs    r0, #0x47
    strb    r0, [sp, #24]
    mov     r0, r11
    add     r1, sp, #24
    call    OBJ_INIT
    add.w   r0, r11, #0x18
    call    EVL_CLEAR
    mov     r0, r11
    mov     r1, r5
    call    OBJ_COPY
    mov     r10, #0
2:
    add.w   r0, r11, #0x18
    mov     r1, r10
    mov     r2, sp                  @ event: +4 position, +0xC handle
    call    EVL_GET
    cbz     r0, 3f
    add     r10, r10, #1
    ldr     r0, [sp, #4]
    cmp     r0, r7
    blo     2b
    add.w   r0, r5, #0x18
    ldr     r1, [sp, #12]
    call    EVL_DEL
    b       2b
3:
    mov     r0, r5
    movs    r1, #0x86
    mov     r2, r6
    call    P_SET
    mov     r0, r4
    add     r1, sp, #32
    mov     r2, r9
    call    SEQ_PUSH
    movs    r0, #0x66               @ refresh, as stock Double posts it
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    orr     r0, r8, #0x100
    strh    r0, [sp, #8]
    movs    r0, #1
    str     r0, [sp, #12]
    movs    r0, #0
    str     r0, [sp, #16]
    str     r0, [sp, #20]
    add.w   r0, r4, #0x30
    mov     r1, sp
    call    UI_POST
    add.w   r0, r4, #0x8000
    movs    r1, #1
    strb.w  r1, [r0, #0xCC4]        @ unsaved changes
    mov     r0, r4
    call    SONG_RELEN
9:
    add     sp, #44
    pop     {r4, r5, r6, r7, r8, r9, r10, r11, pc}

@ seq_bar_label(seq widget): "N bar" or "N.N" on the button above UNDO.
@ Frame: [sp] snprintf's 5th argument, sp+16 text, sp+40 ref.
.thumb_func
seq_bar_label:
    push    {r4, r5, r6, lr}
    sub     sp, #48
    mov     r4, r0
    add.w   r1, r4, #0x1000
    ldrb.w  r1, [r1, #(SEQ_KEY - 0x1000)]
    ldr     r0, =APP
    add     r3, sp, #40
    bl      seq_layer
    mov     r5, r0
    movs    r1, #0x86
    call    P_GET
    mov     r6, r0
    mov     r0, r5
    movs    r1, #0x85
    call    P_GET
    cmp     r0, #15
    it      hs
    movhs   r0, #10
    ldr     r1, =STEP_TABLE
    add.w   r1, r1, r0, lsl #2
    vldr    s0, [r1]
    vmov    s1, r6
    vcvt.f32.s32 s1, s1
    vmul.f32 s0, s0, s1
    vldr    s1, tenths_per_beat
    vmul.f32 s0, s0, s1
    vmov.f32 s1, #0.5
    vadd.f32 s0, s0, s1
    vcvt.s32.f32 s0, s0
    vmov    r3, s0                  @ bars x 10
    movs    r1, #10
    udiv    r2, r3, r1              @ whole bars
    mls     r3, r2, r1, r3          @ tenths
    str     r3, [sp]
    cmp     r3, #0
    mov     r3, r2
    ite     eq
    adreq   r2, fmt_bars
    adrne   r2, fmt_frac
    add     r0, sp, #16
    movs    r1, #16
    call    SNPRINTF
    ldr     r0, =SEQ_BARS
    add     r0, r4
    add     r1, sp, #16
    call    BTN_TEXT
    add     sp, #48
    pop     {r4, r5, r6, pc}

.align 2
fmt_bars: .asciz "%d bar"
.align 2
fmt_frac: .asciz "%d.%d"
.align 2
.ltorg
