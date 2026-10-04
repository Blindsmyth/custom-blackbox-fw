@ 3.1.U: Clip-only Repitch enum on stock 3.1.9.
@
@ UI / XML: register id 0x19C (repitch), put it in the pad bag, show it as a
@ fifth Pos-tab row on Clip pads. Save/load is the stock named-bag dump.
@
@ Audio: Clip playback is the type-0x4C engine, malloc 3000 (0xBB8).
@ Stock uses through +0xBB0 (dirty). +0xBB1/+0xBB4 are padding.
@ FUN_08065550 stores the rate at grain+0x48 with r3 = grain
@ (0x08065756). That hook must not clobber r3. 0x19C lives at +0xBB1;
@ tempo/original scale at +0xBB4 after FUN_08064a94.

.syntax unified
.thumb
.cpu cortex-m7
.fpu fpv5-sp-d16

.equ STATE,       0x38000000
.equ MAGIC,       0x52505431
.equ S_FLAGS,     0x10

.equ PARAM_ENUM,  0x0808C0D9
.equ PARAM_ADD,   0x08093EF7
.equ PARAM_SET,   0x08093ECD
.equ PARAM_GET,   0x08093E9D
.equ POST_CONT,   0x0804C5A1
.equ SET_CONT,    0x08093ED3
.equ CLIPP_CONT,  0x0806764F
.equ RATE_CONT,   0x08064A99
.equ CLIP_GRAIN,  0x08065291

.equ ID_REPITCH,  0x19C
.equ CLIP_FLAG,   0xBB1
.equ CLIP_SCALE,  0xBB4

.macro call addr
    ldr r12, =\addr
    blx r12
.endm

.global hook_register
.global hook_template
.global hook_clip_def
.global hook_post
.global hook_set
.global hook_clip_param
.global hook_rate
.global hook_main_rate
.global hook_grain
.global state_init

@ ---------------------------------------------------------------------------
@ After the last stock Param_RegisterEnum (id 0x19B). Args for that call are
@ still in r0-r3 and [sp]/[sp,#4]. Then register 0x19C the same way.
@ ---------------------------------------------------------------------------
.thumb_func
hook_register:
    push    {r4, r5, r6, r7, r8, lr}
    ldr     r4, [sp, #24]
    ldr     r5, [sp, #28]
    sub     sp, #8
    str     r4, [sp]
    str     r5, [sp, #4]
    call    PARAM_ENUM
    bl      state_init
    mov     r0, r8
    movw    r1, #ID_REPITCH
    adr     r2, title_repitch
    adr     r3, labels_repitch
    movs    r4, #2
    adr     r5, xml_repitch
    str     r4, [sp]
    str     r5, [sp, #4]
    call    PARAM_ENUM
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, pc}

.thumb_func
state_init:
    ldr     r0, =STATE
    ldr     r1, =MAGIC
    ldr     r2, [r0]
    cmp     r1, r2
    beq     2f
    str     r1, [r0]
    adds    r3, r0, #S_FLAGS
    movs    r1, #0
    movs    r2, #0
1:
    str     r1, [r3, r2]
    adds    r2, #4
    cmp     r2, #0x100
    blo     1b
2:
    bx      lr

@ ---------------------------------------------------------------------------
@ Last pad-template insert (id 0xDC). Append 0x19C=0 so load walks it.
@ ---------------------------------------------------------------------------
.thumb_func
hook_template:
    push    {r0, r4, lr}
    call    PARAM_ADD
    pop     {r4}
    mov     r0, r4
    movw    r1, #ID_REPITCH
    movs    r2, #0
    call    PARAM_ADD
    pop     {r4, pc}

@ ---------------------------------------------------------------------------
@ Clip branch of Cell_ApplyModeDefaults, after beatcount=0. Insert 0x19C if
@ the bag does not already have it (do not overwrite a loaded 1). r5 is still
@ the slot.
@ ---------------------------------------------------------------------------
.thumb_func
hook_clip_def:
    push    {r0, r4, r5, r6, lr}
    call    PARAM_SET
    pop     {r4}
    ldrh    r6, [r5, #4]
    mov     r0, r4
    movw    r1, #ID_REPITCH
    bl      bag_has
    cmp     r0, #0
    bne     1f
    mov     r0, r4
    movw    r1, #ID_REPITCH
    movs    r2, #0
    call    PARAM_ADD
1:
    mov     r0, r4
    movw    r1, #ID_REPITCH
    call    PARAM_GET
    mov     r1, r6
    bl      pack_key_packed
    bl      flag_write
    pop     {r4, r5, r6, pc}

@ r0 = cell, r1 = id. Returns 1 if the id is present (value may be 0).
.thumb_func
bag_has:
    ldrh    r2, [r0, #10]
    cbz     r2, 2f
    ldr     r3, [r0, #4]
    cbz     r3, 2f
0:
    ldrh    r0, [r3]
    cmp     r0, r1
    beq     1f
    adds    r3, #8
    subs    r2, #1
    bne     0b
2:
    movs    r0, #0
    bx      lr
1:
    movs    r0, #1
    bx      lr

@ ---------------------------------------------------------------------------
@ Engine_PostParam entry. Record 0x19C, then the stolen 4 bytes, then the rest.
@ r1 is unpacked (col | row<<8 | layer<<16).
@ ---------------------------------------------------------------------------
.thumb_func
hook_post:
    push    {r0, r1, r2, r3, lr}
    movw    r0, #ID_REPITCH
    cmp     r2, r0
    bne     1f
    mov     r0, r3
    bl      pack_key_unpacked
    bl      flag_write
1:
    pop     {r0, r1, r2, r3, lr}
    push    {lr}
    sub     sp, #0x1C
    ldr     r12, =POST_CONT
    bx      r12

@ Param_Set entry. Insert 0x19C if the bag does not have it, then stock SET.
.thumb_func
hook_set:
    push    {r0, r1, r2, r3, lr}
    movw    r3, #ID_REPITCH
    cmp     r1, r3
    bne     1f
    bl      bag_has
    cmp     r0, #0
    bne     1f
    pop     {r0, r1, r2, r3, lr}
    push    {r0, r1, r2, r3, lr}
    call    PARAM_ADD
1:
    pop     {r0, r1, r2, r3, lr}
    push    {r4, lr}
    ldrh.w  lr, [r0, #0xa]
    ldr     r12, =SET_CONT
    bx      r12

@ Clip-engine param switch (FUN_08067648). Record 0x19C and return; stock
@ ignores the id. Do not tail into 64a94 here (mode switch can call this
@ on a clip that is still being rebuilt).
.thumb_func
hook_clip_param:
    push    {r0, r1, r2, r3, r4, lr}
    ldr     r4, [r2, #0xc]
    uxth    r4, r4
    movw    r3, #ID_REPITCH
    cmp     r4, r3
    bne     1f
    ldr     r4, [r2, #0x10]
    strb    r4, [r0, #CLIP_FLAG]
    ldr     r1, [r0, #0x18]
    bl      pack_key_unpacked
    mov     r0, r4
    bl      flag_write
    pop     {r0, r1, r2, r3, r4, pc}
1:
    pop     {r0, r1, r2, r3, r4, lr}
    push    {r4, r5, r6, r7, lr}
    ldr.w   lr, [r2, #0xc]
    ldr     r12, =CLIPP_CONT
    bx      r12

@ FUN_08064a94 entry. Run stock, copy SRAM4 onto +0xBB1, then
@ clip+0xBB4 = tempo / +0x258 when Repitch. r1 may be 0 on the
@ early-return path (no sample yet).
.thumb_func
hook_rate:
    push    {r0, r1, r2, r3, r4, r5, lr}
    mov     r4, r0
    mov     r5, r1
    bl      go_stock_rate
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #1
    beq     2f
    ldr     r1, [r4, #0x18]
    bl      pack_key_unpacked
    bl      flag_read
    cmp     r0, #1
    bne     3f
    movs    r0, #1
    strb    r0, [r4, #CLIP_FLAG]
2:
    cmp     r5, #0
    beq     3f
    vldr    s0, [r5, #0x18]
    vldr    s1, [r4, #0x258]
    vcmp.f32 s1, #0.0
    vmrs    apsr_nzcv, fpscr
    beq     3f
    vdiv.f32 s0, s0, s1
    addw    r0, r4, #CLIP_SCALE
    vstr    s0, [r0]
    b       4f
3:
    mov     r0, #0x3f800000
    addw    r1, r4, #CLIP_SCALE
    str     r0, [r1]
4:
    pop     {r0, r1, r2, r3, r4, r5, pc}

.thumb_func
go_stock_rate:
    push.w  {r4, r5, r6, r7, r8, sb, lr}
    ldr     r12, =RATE_CONT
    bx      r12

@ Main Clip_StartGrain rate store (vstr s15, [r3, #0x48]). r3 is the grain
@ and r4 is the clip. Do not call pack_key: it uses r3.
.thumb_func
hook_main_rate:
    push    {r0, lr}
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #1
    bne     1f
    addw    r0, r4, #CLIP_SCALE
    vldr    s0, [r0]
    vmul.f32 s15, s15, s0
1:
    vstr    s15, [r3, #0x48]
    pop     {r0, pc}

@ Slicer Clip_StartGrain: apply the same scale and drop stretch pad +0x60.
.thumb_func
hook_grain:
    push    {r1, r2, r3, r4, r7, lr}
    mov     r4, r0
    call    CLIP_GRAIN
    mov     r7, r0
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #1
    bne     1f
    movs    r0, #0x70
    mul     r0, r7, r0
    add     r0, r4
    vldr    s0, [r0, #0x48]
    addw    r1, r4, #CLIP_SCALE
    vldr    s1, [r1]
    vmul.f32 s0, s0, s1
    vstr    s0, [r0, #0x48]
    movs    r1, #0
    str     r1, [r0, #0x60]
1:
    mov     r0, r7
    pop     {r1, r2, r3, r4, r7, pc}

@ r0 = value, r1 = 0..15 key. No-op until state_init writes the magic.
.thumb_func
flag_write:
    push    {r2, r3, lr}
    ldr     r2, =STATE
    ldr     r3, [r2]
    ldr     r2, =MAGIC
    cmp     r3, r2
    bne     1f
    ldr     r2, =STATE + S_FLAGS
    uxtb    r0, r0
    uxtb    r1, r1
    strb    r0, [r2, r1]
1:
    pop     {r2, r3, pc}

@ r1 = key. Returns 0/1.
.thumb_func
flag_read:
    ldr     r0, =STATE
    ldr     r2, [r0]
    ldr     r3, =MAGIC
    cmp     r2, r3
    bne     1f
    adds    r0, #S_FLAGS
    uxtb    r1, r1
    ldrb    r0, [r0, r1]
    bx      lr
1:
    movs    r0, #0
    bx      lr

@ r1 unpacked (col | row<<8) -> r1 = col | (row << 2). Uses r3.
.thumb_func
pack_key_unpacked:
    lsr     r3, r1, #8
    and     r3, r3, #0xf
    lsl     r3, r3, #2
    and     r1, r1, #0xf
    orr     r1, r1, r3
    bx      lr

@ r1 packed ushort (col | row<<4) -> r1 = col | (row << 2). Uses r3.
.thumb_func
pack_key_packed:
    lsr     r3, r1, #4
    and     r3, r3, #0xf
    lsl     r3, r3, #2
    and     r1, r1, #0xf
    orr     r1, r1, r3
    bx      lr

.align 2
title_repitch:
    .asciz  "Repitch:"
.align 2
xml_repitch:
    .asciz  "repitch"
.align 2
str_stretch:
    .asciz  "Stretch"
.align 2
str_repitch:
    .asciz  "Repitch"
.align 2
labels_repitch:
    .word   str_stretch
    .word   str_repitch
