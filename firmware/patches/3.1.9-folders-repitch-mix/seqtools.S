@ Seq screen: the length field above UNDO / CLR / OFF is the sequence's Rec Len (seqrec.S).
@ - Seq onEvent wrapper (vtable 0x080F0E90 +0x34). Encoders 0/1 no longer navigate and 3 no
@   longer swaps the side panel (Batch 0). Encoder 2 (top right) is the length knob:
@   * speed as the stock Step Len list: one step per 400 encoder units (FUN_080B0B74);
@   * a length equal to a Rec Len preset (1/8 .. 32 bars) steps to the next/previous preset
@     and stores it as Rec Len; any other (polymetric) length doubles / halves: x2 = count x2
@     (or a 2x longer step past 512), /2 = count /2 when even, else a 2x shorter step;
@   * doubling copies the notes into the new half when Tools -> Rec -> Dbl Copy is On
@     (as stock Double: scratch copy, re-add at +old length, push the layer, undo snapshot);
@   * halving never deletes notes. The knob never selects Custom (settings page only).
@ - The unlabelled button above UNDO (Seq+0x3F70) shows the length: a preset name ("2 bar"),
@   otherwise count/denominator ("15/16", "63/16"), "Nst" for triplet/bar steps or when that
@   doesn't fit five characters, and "Cust" for an empty Custom sequence.
@ - While SEQS is held the layer panel (A-D, Seq+0x4010) shows instead of the OFF/UNDO/CLR
@   panel (Seq+0x3D5C); tap a layer there. Stock swaps them by their hidden bytes (+0x30).

.equ SEQ_EVENT,   0x080B5285
.equ SEQ_KEY,     0x1A90                @ selected sequence pad (byte)
.equ SEQ_BARS,    0x3F70                @ button above UNDO

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

.equ MX_SEQ_PANEL, 40                   @ MX_STATE: 1 while the layer panel is shown
.equ MX_SQ_ACC,   68                    @ MX_STATE: length knob accumulator
.equ MX_SQ_KEY,   72                    @ MX_STATE: sequence the accumulator belongs to
.equ SQ_UNITS,    400                   @ encoder units per step (stock list sliders)
.equ EVL_ADD,     0x08063B29            @ (list, &event)
.equ APP_HANDLE,  0xE808                @ next event handle
.equ SEQ_TICK,    0x080B55F5
.equ SEQ_PANEL_A, 0x3D5C
.equ SEQ_PANEL_B, 0x4010
.equ BTN_SEQS_LV, 0x24009AEE

.global seq_on_event
.global seq_tick
.global seq_bar_label

.thumb_func
seq_on_event:
    ldrh    r2, [r1]
    cmp     r2, #0x32
    bne     5f
    ldrh    r2, [r1, #0xC]
    cmp     r2, #2
    bne     9f                      @ 0, 1: navigation; 3: panel swap. All off.
    ldrsh.w r1, [r1, #0x10]
    b       seq_len_knob
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

@ tick(seq, flags): stock, then show the layer panel while SEQS is held (padsel.S).
.thumb_func
seq_tick:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    call    SEQ_TICK
    movw    r0, #SEQ_PANEL_A
    movw    r1, #SEQ_PANEL_B
    push    {r0, r1}
    mov     r0, r4
    mov     r1, r5
    ldr     r2, =BTN_SEQS_LV
    movs    r3, #MX_SEQ_PANEL
    bl      hold_panel
    add     sp, #8
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

@ r0 = seq widget, r1 = ref out (8 bytes). Fills the selected sequence's ref.
.thumb_func
sq_ref:
    add.w   r2, r0, #0x1000
    ldrb.w  r2, [r2, #(SEQ_KEY - 0x1000)]
    orr     r2, r2, #0x100
    ldr     r3, =REF_TAG
    str     r3, [r1]
    strh    r2, [r1, #4]
    bx      lr

@ sq_len(ref r0) -> r0 active layer, r1 count (0x86), r2 step index (0x85), r3 step ticks.
@ Uses r4-r7 internally (saved).
.thumb_func
sq_len:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #4
    mov     r1, r0
    ldr     r0, =APP
    bl      sr_layers
    mov     r4, r0                  @ active layer
    movs    r1, #0x86
    call    P_GET
    mov     r5, r0
    mov     r0, r4
    movs    r1, #0x85
    call    P_GET
    cmp     r0, #15
    it      hs
    movhs   r0, #10
    mov     r6, r0
    mov     r0, r4
    bl      step_ticks
    mov     r3, r0
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    add     sp, #4
    pop     {r4, r5, r6, r7, pc}

@ r0 = length in ticks. Returns r0 = Rec Len preset index 1..9 with that length, or 0.
.thumb_func
sq_preset_of:
    adr     r1, sr_len_ticks_q
    movs    r2, #1
1:
    ldr     r3, [r1, r2, lsl #2]
    cmp     r3, r0
    beq     2f
    adds    r2, #1
    cmp     r2, #10
    blo     1b
    movs    r0, #0
    bx      lr
2:
    mov     r0, r2
    bx      lr

.align 2
sr_len_ticks_q:                         @ same as seqrec.S sr_len_ticks
    .word   0, 480, 960, 1920, 3840, 7680, 15360, 30720, 61440, 122880
sq_dbl_idx:                             @ step index 2x longer, or 0xFF
    .byte   0xFF, 0, 1, 2, 3, 0xFF, 4, 5, 6, 7, 8, 9, 10, 11, 12
sq_half_idx:                            @ step index 2x shorter, or 0xFF
    .byte   1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0xFF, 0xFF
sq_den:                                 @ label denominator per step index, 0 = "st"
    .byte   0, 0, 0, 1, 2, 0, 4, 0, 8, 0, 16, 0, 32, 0, 64
.align 2

@ seq_len_knob(seq r0, delta r1): tail-called from seq_on_event for encoder 2.
.thumb_func
seq_len_knob:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #12
    mov     r4, r0
    mov     r5, r1
    mov     r1, sp
    bl      sq_ref
    bl      mx_state
    mov     r6, r0
    ldrh    r1, [sp, #4]
    ldr     r2, [r6, #MX_SQ_KEY]
    cmp     r1, r2
    beq     1f
    str     r1, [r6, #MX_SQ_KEY]    @ another sequence: start over
    movs    r2, #0
    str     r2, [r6, #MX_SQ_ACC]
1:
    ldr     r7, [r6, #MX_SQ_ACC]
    add     r7, r5
2:
    cmp     r7, #SQ_UNITS
    blt     3f
    subs    r7, #SQ_UNITS
    mov     r0, sp
    movs    r1, #1
    bl      sq_step
    b       2b
3:
    cmn     r7, #SQ_UNITS
    bgt     4f
    adds    r7, #SQ_UNITS
    mov     r0, sp
    mov     r1, #-1
    bl      sq_step
    b       3b
4:
    str     r7, [r6, #MX_SQ_ACC]
    mov     r0, r4
    bl      seq_bar_label
    add     sp, #12
    pop     {r4, r5, r6, r7, pc}

@ sq_step(ref r0, dir r1 +1/-1): one knob step on the sequence's length.
.thumb_func
sq_step:
    push    {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    sub     sp, #12
    mov     r4, r0                  @ ref
    mov     r5, r1                  @ dir
    bl      sq_len
    mov     r6, r0                  @ active layer
    mov     r7, r1                  @ count
    mov     r8, r2                  @ step index
    mov     r9, r3                  @ step ticks
    mul     r0, r7, r9
    bl      sq_preset_of
    mov     r10, r0                 @ preset or 0
    cmp     r10, #0
    beq     5f
    cmp     r5, #0                  @ preset: next/previous preset
    blt     2f
    cmp     r10, #9
    bhs     9f
    bl      sq_double
    add     r3, r10, #1
    b       3f
2:
    cmp     r10, #1
    bls     9f
    bl      sq_halve
    sub     r3, r10, #1
3:
    ldr     r0, =APP                @ store the new preset as Rec Len, touched
    mov     r1, r4
    movw    r2, #SR_LEN
    bl      sr_setp
    ldr     r0, =APP
    mov     r1, r4
    movw    r2, #SR_SET
    movs    r3, #1
    bl      sr_setp
    b       9f
5:
    cmp     r5, #0                  @ polymetric: x2 / /2
    blt     6f
    bl      sq_double
    b       9f
6:
    bl      sq_halve
9:
    add     sp, #12
    pop     {r4, r5, r6, r7, r8, r9, r10, r11, pc}

@ Called from sq_step with r4 ref, r6 layer, r7 count, r8 step index, r9 step ticks.
@ x2: count x2 (copying the notes if Dbl Copy is On), or a 2x longer step past 512.
.thumb_func
sq_double:
    push    {r10, r11, lr}
    sub     sp, #36
    lsls    r0, r7, #1
    cmp     r0, #512
    bgt     7f
    ldr     r0, [r6, #0x1C]         @ events
    cmp     r0, #0
    beq.w   6f
    ldr     r0, =GLOBAL_BAG
    movw    r1, #SR_DBL
    call    P_GET
    cmp     r0, #0
    beq.w   6f
    ldr     r0, =APP                @ copy the notes into the new half
    mov     r1, r4
    call    SEQ_SNAP
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0
    call    SEQ_OBJ                 @ layer 0: step mode?
    movs    r1, #0x88
    call    P_GET
    mov.w   r10, #960
    cmp     r0, #0
    it      eq
    moveq   r10, r9
    mul     r10, r10, r7            @ old length in event position units
    ldr     r11, =APP + APP_SCRATCH
    movs    r0, #0x47
    strb    r0, [sp, #32]
    mov     r0, r11
    add     r1, sp, #32
    call    OBJ_INIT
    add.w   r0, r11, #0x18
    call    EVL_CLEAR
    mov     r0, r11
    mov     r1, r6
    call    OBJ_COPY
    movs    r0, #0
    str     r0, [sp, #28]           @ index
1:
    add.w   r0, r11, #0x18
    ldr     r1, [sp, #28]
    mov     r2, sp
    call    EVL_GET
    cbz     r0, 2f
    ldr     r0, [sp, #28]
    adds    r0, #1
    str     r0, [sp, #28]
    ldr     r0, [sp, #4]
    add     r0, r10
    str     r0, [sp, #4]            @ position + old length
    movs    r0, #0
    str     r0, [sp, #12]           @ handle: renumbered by the push
    ldr     r0, [r6, #0x1C]
    cmp     r0, #512
    bhs     2f
    add.w   r0, r6, #0x18
    mov     r1, sp
    call    EVL_ADD
    b       1b
2:
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0x86
    lsls    r3, r7, #1
    bl      sr_setp
    ldr     r0, =APP                @ the UI list grew: push the whole layer
    mov     r1, r4
    bl      sr_layers
    ldr     r0, =APP
    mov     r1, r4
    call    SEQ_PUSH
    movs    r0, #0x66               @ refresh, as stock Double posts it
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    ldrh    r0, [r4, #4]
    strh    r0, [sp, #8]
    movs    r0, #1
    str     r0, [sp, #12]
    movs    r0, #0
    str     r0, [sp, #16]
    str     r0, [sp, #20]
    ldr     r0, =APP + 0x30
    mov     r1, sp
    call    UI_POST
    ldr     r0, =APP + 0x8CC4
    movs    r1, #1
    strb    r1, [r0]                @ unsaved changes
    ldr     r0, =APP
    call    SONG_RELEN
    b       9f
6:
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0x86
    lsls    r3, r7, #1
    bl      sr_setp
    b       9f
7:
    adr     r0, sq_dbl_idx          @ count would pass 512: 2x longer step
    ldrb    r3, [r0, r8]
    cmp     r3, #0xFF
    beq     9f
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0x85
    bl      sr_setp
9:
    add     sp, #36
    pop     {r10, r11, pc}

@ /2: count /2 when even, else a 2x shorter step. Never deletes notes.
.thumb_func
sq_halve:
    push    {r10, lr}
    tst     r7, #1
    bne     1f
    cmp     r7, #2
    blt     9f
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0x86
    lsrs    r3, r7, #1
    bl      sr_setp
    b       9f
1:
    adr     r0, sq_half_idx
    ldrb    r3, [r0, r8]
    cmp     r3, #0xFF
    beq     9f
    ldr     r0, =APP
    mov     r1, r4
    movs    r2, #0x85
    bl      sr_setp
9:
    pop     {r10, pc}

@ seq_bar_label(seq widget): the length field above UNDO.
@ Frame: [sp] snprintf 5th arg, sp+8 ref, sp+16 text (16 bytes).
.thumb_func
seq_bar_label:
    push    {r4, r5, r6, r7, r8, lr}
    sub     sp, #40
    mov     r4, r0
    add     r1, sp, #8
    bl      sq_ref
    ldr     r0, =APP
    add     r1, sp, #8
    movw    r2, #SR_LEN
    bl      sr_getp
    mov     r5, r0                  @ Rec Len
    add     r0, sp, #8
    bl      sq_len
    mov     r6, r1                  @ count
    mov     r7, r2                  @ step index
    ldr     r0, [r0, #0x1C]         @ events
    cbnz    r5, 1f
    cbnz    r0, 1f
    adr     r1, s_cust              @ empty Custom sequence
    b       8f
1:
    mul     r0, r6, r3
    bl      sq_preset_of
    cbz     r0, 2f
    adr     r1, sr_short_labels
    ldr     r1, [r1, r0, lsl #2]
    b       8f
2:
    adr     r0, sq_den
    ldrb    r0, [r0, r7]
    cbz     r0, 4f
    str     r0, [sp]
    add     r0, sp, #16
    movs    r1, #16
    adr     r2, fmt_frac_len
    mov     r3, r6
    call    SNPRINTF
    cmp     r0, #5
    ble     5f
4:
    add     r0, sp, #16             @ "Nst"
    movs    r1, #16
    adr     r2, fmt_steps
    mov     r3, r6
    call    SNPRINTF
5:
    add     r1, sp, #16
8:
    ldr     r0, =SEQ_BARS
    add     r0, r4
    call    BTN_TEXT
    add     sp, #40
    pop     {r4, r5, r6, r7, r8, pc}

.align 2
sr_short_labels:                        @ five characters fit the button (12 px each)
    .word   s_cust, s_1_8, s_1_4, s_1_2, s_1bar, s_2bar, s_4bar, s_8bar, s_16bar, s_32bar
s_cust:  .asciz "Cust"
s_2bar:  .asciz "2 bar"
s_4bar:  .asciz "4 bar"
s_8bar:  .asciz "8 bar"
s_16bar: .asciz "16bar"
s_32bar: .asciz "32bar"
.align 2
fmt_frac_len: .asciz "%d/%d"
.align 2
fmt_steps: .asciz "%dst"
.align 2
.ltorg
