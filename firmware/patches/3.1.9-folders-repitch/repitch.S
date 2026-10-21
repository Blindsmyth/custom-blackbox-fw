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
.equ SM_BASE,     0x38007000
.equ SM_OWNER,    4
.equ SM_RESET,    20
.equ SM_BLOCK,    24
.equ SM_AGE,      RP_STATE + 0x110  @ lane calls since the voice owner last mixed
.equ LANE0,       0x260
.equ LANE_STRIDE, 0x410

@ Warp Rubber (3.1.R only: 3.1.9-mix-rubber/rubber_cave.S sets WARP_RUBBER).
@ List is Stretch / Rubber only (no Repitch/Smooth/…). Value 1 hands a Clip's per-lane
@ render to Rubber Band in flash bank 2 (tools/stretch/rubber, tools/bank2).
.equ WARP_REPITCH_V, 1              @ Repitch: the grain rate scaled by the tempo ratio
.ifdef WARP_RUBBER
.ifdef WARP_LINEUP
@ 3.1.T (3.1.9-mix-bungee --lineup): Stutter (stock Stretch) / Fill / Repitch / Slice / Grain /
@ Bungee. Fill, Slice and Grain render each lane in bank 2 (tools/stretch/bungee/lanes.cpp, rb_lanes);
@ Bungee is the one-voice leaf, last, so warp_count can leave it off a pad's list.
.equ WARP_FILL_V, 1
.equ WARP_REPITCH_V, 2
.equ WARP_SLICE_V, 3
.equ WARP_GRAIN_V, 4
.equ WARP_RUBBER_V, 5
.else
.ifdef WARP_KEEP_REPITCH
.equ WARP_RUBBER_V, 2               @ Stretch / Repitch / leaf (the Bungee build with --with-repitch)
.else
.equ WARP_RUBBER_V, 1
.endif
.endif
.equ SM_BASE,     0x38007000        @ RbVoice (tools/stretch/rubber/rubber.h)
.equ SM_OWNER,    4
.equ SM_RESET,    20
.equ SM_BLOCK,    24
.equ SM_LABEL,    32
.equ SM_PREV,     72                @ RbVoice.prev_owner: the Clip a launch took the voice from
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
.ifndef WARP_PLAIN
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
.ifdef WARP_LINEUP
    movs    r4, #6                  @ Stutter, Fill, Repitch, Slice, Grain, Bungee
.else
.ifdef WARP_KEEP_REPITCH
    movs    r4, #3                  @ Stretch, Repitch, leaf
.else
    movs    r4, #2                  @ Stretch, Rubber
.endif
.endif
.else
.ifdef WARP_PLAIN
    movs    r4, #2                  @ 3.1.Q: Stretch, Repitch
.else
    movs    r4, #6                  @ 3.1.Z: Stretch, Repitch, Smooth, Capi, Vari, Grain
.endif
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
.ifdef WARP_RUBBER_V
    @ Every boot: reset the Rubber voice, check bank 2, boot Rubber Band if it is intact.
    bl      rb_pair
.endif
.else
.ifndef WARP_PLAIN
    @ SRAM4 is random after power-on. Without this, SM_OWNER is garbage and
    @ hook_lane always falls through to stock grains (Smooth/Capi/Vari never run).
    ldr     r0, =SM_BASE
    ldr     r2, [r0]
    ldr     r1, =0x534D5448
    cmp     r1, r2
    beq     3f
    ldr     r1, =0x43415049
    cmp     r1, r2
    beq     3f
    ldr     r1, =0x56415249
    cmp     r1, r2
    beq     3f
    bl      smooth_init
3:
.endif
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
.ifdef WARP_LINEUP
    @ Every Warp but Stutter (0) follows the tempo ratio: Repitch scales its grain rate by it,
    @ Fill / Slice / Grain / Bungee render from it in bank 2.
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #0
    bne     2f
    ldr     r1, [r4, #0x18]
    bl      pack_key_unpacked
    bl      flag_read
    cmp     r0, #0
    beq     3f
    cmp     r0, #WARP_RUBBER_V
    bhi     3f
    strb    r0, [r4, #CLIP_FLAG]
    b       2f
.endif
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #1
    beq     2f
.ifdef WARP_RUBBER
.ifdef WARP_RUBBER_V
    cmp     r0, #WARP_RUBBER_V
    beq     2f
.endif
.else
.ifndef WARP_PLAIN
    cmp     r0, #2
    beq     2f
    cmp     r0, #3
    beq     2f
    cmp     r0, #4
    beq     2f
    cmp     r0, #5
    beq     3f
.endif
.endif
    ldr     r1, [r4, #0x18]
    bl      pack_key_unpacked
    bl      flag_read
.ifndef WARP_PLAIN
    cmp     r0, #5
    beq     1f
.endif
    cmp     r0, #1
    beq     1f
.ifdef WARP_RUBBER
.ifdef WARP_RUBBER_V
    cmp     r0, #WARP_RUBBER_V
    beq     1f
.endif
    b       3f
.else
.ifdef WARP_PLAIN
    b       3f
.else
    cmp     r0, #2
    beq     1f
    cmp     r0, #3
    beq     1f
    cmp     r0, #4
    bne     3f
.endif
.endif
1:
    strb    r0, [r4, #CLIP_FLAG]
.ifndef WARP_PLAIN
    cmp     r0, #5
    beq     3f
.endif
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
    @ The latest launch takes the voice at once, so a choke switch between two leaf Clips has
    @ no gap: the leaf ends the previous owner's lanes on its next mix (SM_PREV).
    cmp     r0, #WARP_RUBBER_V
    bne     1f
    ldr     r1, =SM_BASE
    movs    r0, #1
    str     r0, [r1, #SM_RESET]
    ldr     r0, [r1, #SM_OWNER]
    cmp     r0, r4
    beq     2f
    str     r0, [r1, #SM_PREV]
    str     r4, [r1, #SM_OWNER]
    b       2f
1:
.else
.ifndef WARP_PLAIN
    cmp     r0, #2
    beq     0f
    cmp     r0, #3
    beq     0f
    cmp     r0, #4
    bne     1f
0:
    ldr     r1, =SM_BASE
    movs    r0, #1
    str     r0, [r1, #SM_RESET]
    b       2f
1:
.endif
.endif
    cmp     r0, #WARP_REPITCH_V
    bne     2f
    addw    r0, r4, #CLIP_SCALE
    vldr    s0, [r0]
    vmul.f32 s15, s15, s0
2:
    vstr    s15, [r3, #0x48]
.ifndef WARP_RUBBER
.ifndef WARP_PLAIN
    ldrb    r0, [r4, #CLIP_FLAG]
    cmp     r0, #5
    bne     3f
    @ Stock Clip_StartGrain wrote #256 to +0x54/+0x5c; +0x58 = end-256.
    ldr     r0, [r3, #0x58]
    ldr     r1, [r3, #0x54]
    add     r0, r0, r1
    sub.w   r0, r0, #2048
    str     r0, [r3, #0x58]
    mov.w   r0, #2048
    str     r0, [r3, #0x54]
    str     r0, [r3, #0x5c]
3:
.endif
.endif
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
.ifdef WARP_RUBBER
.ifdef WARP_RUBBER_V
    cmp     r2, #WARP_RUBBER_V      @ Rubber on Slicer: stock grains, unscaled
    beq     1f
.endif
.else
.ifndef WARP_PLAIN
    cmp     r2, #5
    beq     2f
.endif
.endif
    cmp     r2, #WARP_REPITCH_V
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
.ifndef WARP_PLAIN
    b       1f
2:
    movs    r2, #0x70
    mul     r2, r0, r2
    add     r2, r1
    ldr     r0, [r2, #0x58]
    ldr     r3, [r2, #0x54]
    add     r0, r0, r3
    sub.w   r0, r0, #2048
    str     r0, [r2, #0x58]
    mov.w   r0, #2048
    str     r0, [r2, #0x54]
    str     r0, [r2, #0x5c]
.endif
1:
    bx      lr

.ifndef WARP_PLAIN
.ifdef WARP_RUBBER
@ Per-lane render FUN_080667B8(clip, ?, lane, ...; mix = arg 6, ctx = arg 7). Rubber on a Clip
@ (not Slicer) renders the whole clip into the mix once per block from the voice owner's lane-0
@ call and skips the stock grains, but only once bank 2 is paired and booted (rb_ready).
@ One voice: a second Rubber pad plays stock grains until the first lets go; an owner that
@ stops rendering (Warp changed, preset reload, clip freed) is replaced after 3 missed calls.
.else
@ Per-lane render. Smooth (2) / Capi (3) / Vari (4) on a Clip write one mix
@ and return. Slicer, Stretch, Repitch, and Grain (5) fall through to stock
@ grains (Grain only rewrote the window in hook_main_rate / grain_after).
@ One shared SM_BASE voice: a second Smooth/Capi/Vari pad keeps stock grains.
.endif
.thumb_func
hook_lane:
    push    {r0, r1, r2, r3, r4, lr}
    ldr     r4, [sp]
    ldrb.w  r0, [r4, #CLIP_FLAG]
.ifdef WARP_LINEUP
    @ Fill / Slice / Grain on a Clip: each lane renders itself in bank 2 (rb_lanes, 1 = done); a
    @ Slicer, or bank 2 not running, plays stock grains.
    cmp     r0, #WARP_FILL_V
    beq     20f
    cmp     r0, #WARP_SLICE_V
    beq     20f
    cmp     r0, #WARP_GRAIN_V
    bne     21f
20:
    ldrb.w  r0, [r4, #0x254]
    cmp     r0, #0
    bne     9f
    ldr     r0, [sp]            @ clip
    ldr     r1, [sp, #8]        @ lane
    ldr     r2, [sp, #28]       @ mix (arg 6)
    ldr     r3, [sp, #32]       @ ctx (arg 7)
    bl      rb_lanes
    cmp     r0, #0
    beq     9f
    pop     {r0, r1, r2, r3, r4, pc}
21:
    ldrb.w  r0, [r4, #CLIP_FLAG]
.endif
.ifdef WARP_RUBBER
    cmp     r0, #WARP_RUBBER_V
    bne     9f
    ldrb.w  r0, [r4, #0x254]            @ Slicer: stock grains
    cmp     r0, #0
    bne     9f
    bl      rb_ready
    cmp     r0, #0
    beq     9f
.else
.ifndef WARP_PLAIN
    cmp     r0, #2
    beq     5f
    cmp     r0, #3
    beq     5f
    cmp     r0, #4
    bne     9f
5:
    ldrb.w  r0, [r4, #0x254]
    cmp     r0, #0
    bne     9f
    ldr     r1, =SM_BASE
    ldrb.w  r0, [r4, #CLIP_FLAG]
    cmp     r0, #3
    beq     6f
    cmp     r0, #4
    beq     10f
    ldr     r0, =0x534D5448
    ldr     r3, [r1]
    cmp     r0, r3
    beq     0f
    mov     r0, r1
    bl      smooth_init
    b       0f
6:
    ldr     r0, =0x43415049
    ldr     r3, [r1]
    cmp     r0, r3
    beq     0f
    mov     r0, r1
    bl      capi_init
    b       0f
10:
    ldr     r0, =0x56415249
    ldr     r3, [r1]
    cmp     r0, r3
    beq     0f
    mov     r0, r1
    bl      vari_init
0:
.endif
.endif
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
.ifdef WARP_RUBBER
    ldr     r0, [r1, #SM_PREV]
    cmp     r0, r4
    beq     8f                          @ lost the voice to a later launch: silent until ended
    cmp     r3, #0
    beq     12f
.else
.ifndef WARP_PLAIN
    cmp     r3, #0
    beq     12f
    @ The owner only lets go from its own lane-0 call with both lanes idle.
    @ If it stopped rendering first (Warp changed, preset reload, clip freed)
    @ the slot stayed taken and every Smooth pad played stock grains. Take it
    @ once the owner has missed a few blocks.
.endif
.endif
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
.ifdef WARP_RUBBER
    ldr     r2, [sp, #28]       @ mix (arg 6)
    ldr     r3, [sp, #32]       @ ctx (arg 7)
    bl      rb_mix
.else
.ifndef WARP_PLAIN
    ldr     r2, [sp, #28]       @ mix (param_6)
    ldr     r3, [sp, #32]       @ ctx (param_7)
    ldrb.w  r4, [r0, #CLIP_FLAG]
    cmp     r4, #3
    beq     7f
    cmp     r4, #4
    beq     11f
    bl      smooth_mix
    b       8f
7:
    bl      capi_mix
    b       8f
11:
    bl      vari_mix
.endif
.endif
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
.ifndef WARP_PLAIN
.align 2
str_smooth:
    .asciz  "Smooth"
.align 2
str_capi:
    .asciz  "Capi"
.align 2
str_vari:
    .asciz  "Vari"
.align 2
str_grain:
    .asciz  "Grain"
.endif
.ifdef WARP_LINEUP
.align 2
str_stutter:
    .asciz  "Stutter"
.align 2
str_fill:
    .asciz  "Fill"
.align 2
str_slice:
    .asciz  "Slice"
.endif
.align 2
labels_repitch:
.ifdef WARP_LINEUP
    .word   str_stutter
    .word   str_fill
    .word   str_repitch
    .word   str_slice
    .word   str_grain
.else
    .word   str_stretch
.endif
.ifdef WARP_RUBBER
.ifndef WARP_LINEUP
.ifdef WARP_KEEP_REPITCH
    .word   str_repitch
.endif
.endif
    .word   SM_BASE + SM_LABEL          @ RAM: "Rubber", "Rubber!" (bank 2 missing) or "Rubber 54%"
.else
    .word   str_repitch
.ifndef WARP_PLAIN
    .word   str_smooth
    .word   str_capi
    .word   str_vari
    .word   str_grain
.endif
.endif
.ifdef WARP_RUBBER
.align 2
.global warp_leaf_v
warp_leaf_v:                            @ the leaf's Warp value, for the bench
    .word   WARP_RUBBER_V
.endif
