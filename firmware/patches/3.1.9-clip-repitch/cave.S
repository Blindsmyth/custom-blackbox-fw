@ 3.1.W: Clip and Slicer Repitch enum on stock 3.1.9.
@
@ UI / XML: register id 0x19C (repitch), put it in the pad bag, show it on
@ the Pos tab for Clip (fifth row) and Slicer (eighth row). Save/load is
@ the stock named-bag dump.
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
.equ S_SAVE,      0x8
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
.equ SM_BASE,     0x38007000
.equ SM_OWNER,    4
.equ SM_RESET,    20
.equ SM_BLOCK,    24
.equ LANE0,       0x260
.equ LANE_STRIDE, 0x410

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
.global hook_lane
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
    movs    r4, #4
    adr     r5, xml_repitch
    str     r4, [sp]
    str     r5, [sp, #4]
    call    PARAM_ENUM
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, pc}

.thumb_func
state_init:
    push    {lr}
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
    @ SRAM4 is random after power-on. Without this, SM_OWNER is garbage and
    @ hook_lane always falls through to stock grains (Smooth/Snap never run).
    ldr     r0, =SM_BASE
    ldr     r2, [r0]
    ldr     r1, =0x534D5448
    cmp     r1, r2
    beq     3f
    ldr     r1, =0x534E4150
    cmp     r1, r2
    beq     3f
    bl      smooth_init
3:
    pop     {pc}

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
@ Clip (beatcount=0) and Slicer (loopmode=0) branches of
@ Cell_ApplyModeDefaults. Insert 0x19C if the bag does not already have it
@ (do not overwrite a loaded 1). r5 is still the slot.
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
    cmp     r0, #2
    beq     2f
    cmp     r0, #3
    beq     2f
    ldr     r1, [r4, #0x18]
    bl      pack_key_unpacked
    bl      flag_read
    cmp     r0, #1
    beq     1f
    cmp     r0, #2
    beq     1f
    cmp     r0, #3
    bne     3f
1:
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
    push    {r0, r1, lr}
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #2
    beq     0f
    cmp     r0, #3
    bne     1f
0:
    ldr     r1, =SM_BASE
    movs    r0, #1
    str     r0, [r1, #SM_RESET]
    b       2f
1:
    cmp     r0, #1
    bne     2f
    addw    r0, r4, #CLIP_SCALE
    vldr    s0, [r0]
    vmul.f32 s15, s15, s0
2:
    vstr    s15, [r3, #0x48]
    pop     {r0, r1, pc}

@ Slicer FUN_08065290 has 10 args (6 on the stack). Do not push before the
@ call: that shifts slice start/length and yields a click with no waveform.
@ Swap lr and stash the clip pointer in SRAM4 instead.
.thumb_func
hook_grain:
    ldr     r12, =STATE + S_SAVE
    str     r0, [r12]
    str     lr, [r12, #4]
    ldr     r12, =grain_after
    orr     r12, r12, #1
    mov     lr, r12
    ldr     r12, =CLIP_GRAIN
    bx      r12
.ltorg

.thumb_func
grain_after:
    ldr     r12, =STATE + S_SAVE
    ldr     r1, [r12]
    ldr     lr, [r12, #4]
    cmp     r0, #0
    blt     1f
    ldrb    r2, [r1, #CLIP_FLAG]
    cmp     r2, #1
    bne     1f
    movs    r2, #0x70
    mul     r2, r0, r2
    add     r2, r1
    vldr    s0, [r2, #0x48]
    addw    r3, r1, #CLIP_SCALE
    vldr    s1, [r3]
    vmul.f32 s0, s0, s1
    vstr    s0, [r2, #0x48]
    movs    r3, #0
    str     r3, [r2, #0x60]
1:
    bx      lr

@ Per-lane render. Smooth (2) / Snap (3) on a Clip write one mix and return.
@ Slicer, Stretch, and Repitch fall through to the stock grains. One shared
@ SM_BASE voice: a second Smooth/Snap pad keeps stock grains until free.
.thumb_func
hook_lane:
    push    {r0, r1, r2, r3, r4, lr}
    ldr     r4, [sp]
    ldrb.w  r0, [r4, #CLIP_FLAG]
    cmp     r0, #2
    beq     5f
    cmp     r0, #3
    bne     9f
5:
    ldrb.w  r0, [r4, #0x254]
    cmp     r0, #0
    bne     9f
    ldr     r1, =SM_BASE
    ldrb.w  r0, [r4, #CLIP_FLAG]
    cmp     r0, #3
    beq     6f
    ldr     r0, =0x534D5448
    ldr     r3, [r1]
    cmp     r0, r3
    beq     0f
    mov     r0, r1
    bl      smooth_init
    b       0f
6:
    ldr     r0, =0x534E4150
    ldr     r3, [r1]
    cmp     r0, r3
    beq     0f
    mov     r0, r1
    bl      snap_init
0:
    addw    r0, r4, #LANE0
    ldr     r2, [sp, #8]
    cmp     r2, r0
    bne     3f
    ldr     r1, =SM_BASE
    movs    r3, #0
    str     r3, [r1, #SM_BLOCK]
    ldrb.w  r3, [r0]
    cmp     r3, #0
    bne     3f
    ldrb.w  r3, [r0, #LANE_STRIDE]
    cmp     r3, #0
    bne     3f
    ldr     r3, [r1, #SM_OWNER]
    cmp     r3, r4
    bne     9f
    movs    r3, #0
    str     r3, [r1, #SM_OWNER]
    b       9f
3:
    ldr     r1, =SM_BASE
    ldr     r3, [r1, #SM_BLOCK]
    cmp     r3, #0
    bne     8f
    ldrb.w  r3, [r2]
    cmp     r3, #0
    beq     9f
    ldrb.w  r3, [r2, #0x148]
    cmp     r3, #0
    bne     9f
    ldr     r3, [r1, #SM_OWNER]
    cmp     r3, r4
    beq     4f
    cmp     r3, #0
    bne     9f
    str     r4, [r1, #SM_OWNER]
4:
    movs    r3, #1
    str     r3, [r1, #SM_BLOCK]
    ldr     r0, [sp]            @ clip
    ldr     r1, [sp, #8]        @ lane
    ldr     r2, [sp, #28]       @ mix (param_6)
    ldr     r3, [sp, #32]       @ ctx (param_7)
    ldrb.w  r4, [r0, #CLIP_FLAG]
    cmp     r4, #3
    beq     7f
    bl      smooth_mix
    b       8f
7:
    bl      snap_mix
8:
    pop     {r0, r1, r2, r3, r4, pc}
9:
    pop     {r0, r1, r2, r3, r4, lr}
    b.w     lane_stock

.thumb_func
lane_stock:
    push.w  {r4, r5, r6, r7, r8, sb, sl, fp, lr}
    ldr     r12, =0x080667BD
    bx      r12
.ltorg

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
    .asciz  "Warp:"
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
str_smooth:
    .asciz  "Smooth"
.align 2
str_snap:
    .asciz  "Snap"
.align 2
labels_repitch:
    .word   str_stretch
    .word   str_repitch
    .word   str_smooth
    .word   str_snap
