@ Seq screen: the length field above UNDO / CLR / OFF is the sequence's Rec Len (seqrec.S).
@ - Seq onEvent wrapper (vtable 0x080F0E90 +0x34). Encoders 0/1 no longer navigate and 3 no
@   longer swaps the side panel (Batch 0). Encoder 2 (top right) steps Rec Len: Custom, 1/8,
@   1/4, 1/2, 1 bar ... 32 bars. A fixed value sets the length (notes past the end are kept).
@ - The unlabelled button above UNDO (Seq+0x3F70) shows Rec Len ("Cust", "1/8" ... "32bar";
@   the button fits five characters).
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

.equ MX_SEQ_T,    24                    @ MX_STATE: uwTick of the last length change
.equ MX_SEQ_PANEL, 40                   @ MX_STATE: 1 while the layer panel is shown
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
    push    {r4, r5, r6, lr}
    sub     sp, #8
    mov     r4, r0
    ldrsh.w r5, [r1, #0x10]
    cbz     r5, 4f
    add.w   r1, r4, #0x1000
    ldrb.w  r1, [r1, #(SEQ_KEY - 0x1000)]
    orr     r1, r1, #0x100
    ldr     r2, =REF_TAG
    str     r2, [sp]
    strh    r1, [sp, #4]            @ ref of the selected sequence
    ldr     r0, =APP
    mov     r1, sp
    movw    r2, #SR_LEN
    bl      sr_getp
    mov     r6, r0
    cmp     r5, #0
    ite     gt
    addgt   r2, r6, #1
    suble   r2, r6, #1
    cmp     r2, #0
    it      lt
    movlt   r2, #0
    cmp     r2, #9
    it      gt
    movgt   r2, #9
    cmp     r2, r6
    beq     4f
    ldr     r0, =APP
    mov     r1, sp
    bl      sr_user_set
    mov     r0, r4
    bl      seq_bar_label
4:
    add     sp, #8
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

@ seq_bar_label(seq widget): the selected sequence's Rec Len on the button above UNDO.
.thumb_func
seq_bar_label:
    push    {r4, r5, r6, lr}
    sub     sp, #8
    mov     r4, r0
    add.w   r1, r4, #0x1000
    ldrb.w  r1, [r1, #(SEQ_KEY - 0x1000)]
    orr     r1, r1, #0x100
    ldr     r2, =REF_TAG
    str     r2, [sp]
    strh    r1, [sp, #4]
    ldr     r0, =APP
    mov     r1, sp
    movw    r2, #SR_LEN
    bl      sr_getp
    cmp     r0, #9
    it      hi
    movhi   r0, #0
    adr     r1, sr_short_labels
    ldr     r1, [r1, r0, lsl #2]
    ldr     r0, =SEQ_BARS
    add     r0, r4
    call    BTN_TEXT
    add     sp, #8
    pop     {r4, r5, r6, pc}

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
.ltorg
