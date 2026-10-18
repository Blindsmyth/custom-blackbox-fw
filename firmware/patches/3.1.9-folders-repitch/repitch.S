@ Clip/Slicer Warp (id 0x19C). SRAM4 after the folder block so fm_init's
@ memset of S_SIZE (0x4930) does not wipe it.
.equ RP_STATE,    0x38005000
.equ RP_MAGIC,    0x52505431
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

@ Warp Rubber (3.1.R only: 3.1.9-mix-rubber/rubber_cave.S sets WARP_RUBBER). Value 2 hands a
@ Clip's per-lane render to Rubber Band in flash bank 2 (tools/stretch/rubber, tools/bank2).
.ifdef WARP_RUBBER
.equ WARP_RUBBER_V, 2
.equ SM_BASE,     0x38007000        @ RbVoice (tools/stretch/rubber/rubber.h)
.equ SM_OWNER,    4
.equ SM_RESET,    20
.equ SM_BLOCK,    24
.equ SM_LABEL,    32
.equ SM_AGE,      RP_STATE + 0x110  @ lane calls since the voice owner last mixed
.equ LANE0,       0x260
.equ LANE_STRIDE, 0x410
.endif

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
.ifdef WARP_RUBBER
.global hook_lane
.endif

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
.ifdef WARP_RUBBER
    movs    r4, #3
.else
    movs    r4, #2
.endif
    adr     r5, xml_repitch
    str     r4, [sp]
    str     r5, [sp, #4]
    call    PARAM_ENUM
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, pc}

.thumb_func
state_init:
    push    {lr}
    ldr     r0, =RP_STATE
    ldr     r1, =RP_MAGIC
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
.ifdef WARP_RUBBER
    @ Every boot: reset the Rubber voice, check bank 2, boot Rubber Band if it is intact.
    bl      rb_pair
.endif
    pop     {pc}

.thumb_func
hook_template:
    push    {r0, r4, lr}
    call    PARAM_ADD
    pop     {r4}
    mov     r0, r4
    movw    r1, #ID_REPITCH
    movs    r2, #0
    call    PARAM_ADD
.ifdef MIX_EXT
    mov     r0, r4                  @ 3.1.Q: Env Trig (adsr.S), default Note
    movw    r1, #0x1A2
    movs    r2, #0
    call    PARAM_ADD
.endif
    pop     {r4, pc}

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
.ifdef MIX_EXT
    ldr     r1, [sp, #4]            @ 3.1.Q: Env Trig (adsr.S)
    ldr     r2, [sp, #8]
    ldr     r3, [sp, #12]
    bl      envtrig_post
.endif
    pop     {r0, r1, r2, r3, lr}
    push    {lr}
    sub     sp, #0x1C
    ldr     r12, =POST_CONT
    bx      r12

.thumb_func
hook_set:
    push    {r0, r1, r2, r3, lr}
    movw    r3, #ID_REPITCH
    cmp     r1, r3
.ifdef MIX_EXT
    beq     3f
    movw    r3, #0x1A2              @ 3.1.Q: Env Trig, added like Warp when a preset lacks it
    cmp     r1, r3
.endif
    bne     1f
3:
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
.ifdef MIX_EXT
    movw    r3, #0x1A2              @ 3.1.Q: Env Trig (adsr.S), swallowed like Warp
    cmp     r4, r3
    bne     2f
    ldr     r4, [r2, #0x10]
    ldr     r1, [r0, #0x18]
    bl      pack_key_unpacked
    mov     r0, r4
    bl      envtrig_write
    pop     {r0, r1, r2, r3, r4, pc}
2:
.endif
    pop     {r0, r1, r2, r3, r4, lr}
    push    {r4, r5, r6, r7, lr}
    ldr.w   lr, [r2, #0xc]
    ldr     r12, =CLIPP_CONT
    bx      r12

.thumb_func
hook_rate:
    push    {r0, r1, r2, r3, r4, r5, lr}
    mov     r4, r0
    mov     r5, r1
    bl      go_stock_rate
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #1
    beq     2f
.ifdef WARP_RUBBER
    cmp     r0, #WARP_RUBBER_V
    beq     2f
.endif
    ldr     r1, [r4, #0x18]
    bl      pack_key_unpacked
    bl      flag_read
    cmp     r0, #1
    beq     1f
.ifdef WARP_RUBBER
    cmp     r0, #WARP_RUBBER_V
    beq     1f
.endif
    b       3f
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

.thumb_func
hook_main_rate:
    push    {r0, r1, lr}
    ldrb    r0, [r4, #CLIP_FLAG]
.ifdef WARP_RUBBER
    @ Rubber: a new grain is a new launch, so the voice restarts (rubber.cpp reset).
    cmp     r0, #WARP_RUBBER_V
    bne     1f
    ldr     r1, =SM_BASE
    movs    r0, #1
    str     r0, [r1, #SM_RESET]
    b       2f
1:
.endif
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
.thumb_func
hook_grain:
    ldr     r12, =RP_STATE + S_SAVE
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
    ldr     r12, =RP_STATE + S_SAVE
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

.ifdef WARP_RUBBER
@ Per-lane render FUN_080667B8(clip, ?, lane, ...; mix = arg 6, ctx = arg 7). Rubber on a Clip
@ (not Slicer) renders the whole clip into the mix once per block from the voice owner's lane-0
@ call and skips the stock grains, but only once bank 2 is paired and booted (rb_ready).
@ One voice: a second Rubber pad plays stock grains until the first lets go; an owner that
@ stops rendering (Warp changed, preset reload, clip freed) is replaced after 3 missed calls.
.thumb_func
hook_lane:
    push    {r0, r1, r2, r3, r4, lr}
    ldr     r4, [sp]
    ldrb.w  r0, [r4, #CLIP_FLAG]
    cmp     r0, #WARP_RUBBER_V
    bne     9f
    ldrb.w  r0, [r4, #0x254]            @ Slicer: stock grains
    cmp     r0, #0
    bne     9f
    bl      rb_ready
    cmp     r0, #0
    beq     9f
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
    beq     12f
    ldr     r0, =SM_AGE
    ldr     r3, [r0]
    adds    r3, #1
    str     r3, [r0]
    cmp     r3, #3
    blo     9f
12:
    str     r4, [r1, #SM_OWNER]
    movs    r3, #1
    str     r3, [r1, #SM_RESET]
4:
    ldr     r0, =SM_AGE
    movs    r3, #0
    str     r3, [r0]
    movs    r3, #1
    str     r3, [r1, #SM_BLOCK]
    ldr     r0, [sp]            @ clip
    ldr     r1, [sp, #8]        @ lane
    ldr     r2, [sp, #28]       @ mix (arg 6)
    ldr     r3, [sp, #32]       @ ctx (arg 7)
    bl      rb_mix
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
.endif

.thumb_func
flag_write:
    push    {r2, r3, lr}
    ldr     r2, =RP_STATE
    ldr     r3, [r2]
    ldr     r2, =RP_MAGIC
    cmp     r3, r2
    bne     1f
    ldr     r2, =RP_STATE + S_FLAGS
    uxtb    r0, r0
    uxtb    r1, r1
    strb    r0, [r2, r1]
1:
    pop     {r2, r3, pc}

.thumb_func
flag_read:
    ldr     r0, =RP_STATE
    ldr     r2, [r0]
    ldr     r3, =RP_MAGIC
    cmp     r2, r3
    bne     1f
    adds    r0, #S_FLAGS
    uxtb    r1, r1
    ldrb    r0, [r0, r1]
    bx      lr
1:
    movs    r0, #0
    bx      lr

.thumb_func
pack_key_unpacked:
    lsr     r3, r1, #8
    and     r3, r3, #0xf
    lsl     r3, r3, #2
    and     r1, r1, #0xf
    orr     r1, r1, r3
    bx      lr

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
labels_repitch:
    .word   str_stretch
    .word   str_repitch
.ifdef WARP_RUBBER
    .word   SM_BASE + SM_LABEL          @ RAM: "Rubber", "Rubber!" (bank 2 missing) or "Rubber 54%"
.endif
