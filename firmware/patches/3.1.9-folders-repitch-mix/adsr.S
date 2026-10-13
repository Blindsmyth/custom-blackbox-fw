@ Clip/Slicer: the amp ADSR as a modulation source (ENV) and slice-triggered envelopes.
@ docs/firmware/README.md "Clip / Slicer engine".
@
@ Envelope (voice+0x258; Clip/Slicer voices at clip+0x260 / +0x670): +4 stage (0 idle,
@ 1 attack, 2 decay, 3 sustain, 4 release, 5 done), +8 settings, +0x28 raw ADSR level 0..1
@ (set every block by FUN_0806A6C0), +0x1C smoothed level applied to the audio.
@ Gate on FUN_0806A5F8, note off FUN_0806A63C, quick release FUN_0806A698.
@
@ ENV source. Mod routing FUN_08067018(clip, msg{+0xC target, +0x10 slot, +0x14 source,
@ +0x16 amount}) binds a target's modparam slot to a float pointer with FUN_0806FCD4(amount,
@ modparam, source, slot) (modparam +8 + slot*8 = source pointer). VEL (0xD) binds both
@ voices' modparams to per-voice floats; the engine reads the pointer every block. ENV runs
@ the VEL path and repoints both slots at that voice's envelope level (+0x28): per voice,
@ so no extra state and no block hook. Same targets as VEL. The source list gains a 9th
@ entry "ENV" (code ENV_CODE); Sample/Multisample pads ignore the code (their handler
@ FUN_08056810 is not hooked yet).
@
@ Env Trig (pad param 0x1A2, Note / Slice, Slicer Conf page after Pad Note). Slice: each
@ time playback crosses into the next slice (bl FUN_08065550 at 0x08066B9A / 0x08066CF2
@ in the lane render FUN_080667B8) the voice's envelope restarts if the note is still held
@ (stage 1-3). The smoothed level is kept, so the amp glides into the new attack instead of
@ dropping to zero. The value reaches the audio task like Warp's (repitch.S hook_post /
@ hook_clip_param, MIX_EXT): a per-pad byte table in SRAM4 keyed by row/col.

.equ ENV_CODE,    0x30                  @ unused by stock (codes 0..0x1D, 0x8000)
.equ ID_ENVTRIG,  0x1A2
.equ ROUTE,       0x08067019            @ FUN_08067018(clip, msg)
.equ SLICE_START, 0x08065551            @ FUN_08065550(clip, voice, ..., 3 stack args)
.equ GATE_ON,     0x0806A5F9            @ FUN_0806A5F8(env)
.equ ENV_OFF,     0x258                 @ voice -> envelope
.equ ET_STATE,    0x38005110            @ SRAM4, after Warp's flags, before MX_STATE
.equ ET_MAGIC,    0x47525445            @ 'ETRG'
.equ ET_SAVE,     4                     @ clip, voice, lr across the slice-start call
.equ ET_TABLE,    0x10                  @ 0x50 bytes, packed key (row << 2 | col)
.equ ET_SIZE,     0x50

.global env_route
.global env_slice
.global envtrig_write
.global envtrig_read
.global envtrig_post
.global env_src_names
.global env_src_codes

@ ---- Env Trig table (any task; bytes only)

@ envtrig_write(value r0, packed key r1). Initialises the table on first use.
.thumb_func
envtrig_write:
    push    {r2, r3, lr}
    cmp     r1, #ET_SIZE
    bhs     9f
    ldr     r2, =ET_STATE
    ldr     r3, [r2]
    push    {r0}
    ldr     r0, =ET_MAGIC
    cmp     r3, r0
    beq     2f
    str     r0, [r2]
    adds    r2, #ET_TABLE
    movs    r0, #0
    movs    r3, #0
1:
    str     r0, [r2, r3]
    adds    r3, #4
    cmp     r3, #ET_SIZE
    blo     1b
    subs    r2, #ET_TABLE
2:
    pop     {r0}
    adds    r2, #ET_TABLE
    strb    r0, [r2, r1]
9:
    pop     {r2, r3, pc}

@ envtrig_read(packed key r1) -> r0 (0 Note, 1 Slice). Leaf, uses r0, r2, r3.
.thumb_func
envtrig_read:
    cmp     r1, #ET_SIZE
    bhs     8f
    ldr     r2, =ET_STATE
    ldr     r3, [r2]
    ldr     r0, =ET_MAGIC
    cmp     r3, r0
    bne     8f
    adds    r2, #ET_TABLE
    ldrb    r0, [r2, r1]
    bx      lr
8:
    movs    r0, #0
    bx      lr

@ envtrig_post(key r1, id r2, value r3): from repitch.S hook_post (Engine_PostParam).
.thumb_func
envtrig_post:
    movw    r0, #ID_ENVTRIG
    cmp     r2, r0
    bne     9f
    push    {lr}
    mov     r0, r3
    bl      pack_key_unpacked
    bl      envtrig_write
    pop     {lr}
9:
    bx      lr

@ ---- slice-triggered envelope (audio task)

@ bl sites 0x08066B9A / 0x08066CF2: FUN_08065550(clip r0, voice r1, r2, r3, 3 stack args).
@ No push before the call (the stack args sit at sp); clip, voice and lr wait in SRAM4.
.thumb_func
env_slice:
    ldr     r12, =ET_STATE + ET_SAVE
    str     r0, [r12]
    str     r1, [r12, #4]
    str     lr, [r12, #8]
    ldr     r12, =env_slice_after
    orr     r12, r12, #1
    mov     lr, r12
    ldr     r12, =SLICE_START
    bx      r12
.ltorg

.thumb_func
env_slice_after:
    push    {r0, r4, r5, r6}
    adds    r1, r0, #1
    beq     9f                      @ no grain started
    ldr     r12, =ET_STATE + ET_SAVE
    ldr     r4, [r12]               @ clip
    ldr     r5, [r12, #4]           @ voice
    ldr     r1, [r4, #0x18]         @ engine pad id: bank << 16 | row << 8 | col
    bl      pack_key_unpacked
    bl      envtrig_read
    cmp     r0, #1
    bne     9f
    add     r5, r5, #ENV_OFF
    ldrb    r0, [r5, #4]
    subs    r0, #1
    cmp     r0, #2
    bhi     9f                      @ only while the note is held
    ldr     r6, [r5, #0x1C]         @ keep the smoothed level: no click
    mov     r0, r5
    ldr     r12, =GATE_ON
    blx     r12
    str     r6, [r5, #0x1C]
9:
    pop     {r0, r4, r5, r6}
    ldr     r12, =ET_STATE + ET_SAVE
    ldr     lr, [r12, #8]
    bx      lr
.ltorg

@ ---- ENV source (audio task)

@ bl site 0x080685C4: FUN_08067018(clip, msg). ENV: VEL's per-voice binding, then both
@ voices' slot pointers -> that voice's envelope level.
.thumb_func
env_route:
    ldrh    r2, [r1, #0x14]
    cmp     r2, #ENV_CODE
    beq     1f
    ldr     r12, =ROUTE
    bx      r12
1:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    movs    r2, #0xD                @ as VEL
    strh    r2, [r5, #0x14]
    ldr     r12, =ROUTE
    blx     r12
    movs    r2, #ENV_CODE
    strh    r2, [r5, #0x14]
    ldr     r6, [r5, #0x10]
    uxth    r6, r6                  @ slot
    cmp     r6, #3
    bhs     9f
    ldrh    r0, [r5, #0xC]          @ target id
    adr     r1, env_targets
2:
    ldrh    r2, [r1]
    cbz     r2, 9f
    cmp     r2, r0
    beq     3f
    adds    r1, #6
    b       2b
3:
    lsls    r6, r6, #3
    adds    r6, #8                  @ modparam + 8 + slot * 8: source pointer
    ldrh    r2, [r1, #2]            @ voice 0 modparam
    add     r2, r4
    add.w   r3, r4, #(0x260 + ENV_OFF + 0x28)
    str     r3, [r2, r6]
    ldrh    r2, [r1, #4]            @ voice 1 modparam
    add     r2, r4
    add.w   r3, r4, #(0x670 + ENV_OFF + 0x28)
    str     r3, [r2, r6]
9:
    pop     {r4, r5, r6, pc}

.align 2
env_targets:                            @ target id, voice 0 / voice 1 modparam (VEL's list)
    .hword  0xC8, 0x448, 0x858          @ Cutoff
    .hword  0x11, 0x484, 0x894
    .hword  0x02, 0x2B8, 0x6C8
    .hword  0x04, 0x2B8, 0x6C8
    .hword  0x3C, 0x514, 0x924          @ Attack
    .hword  0x3E, 0x548, 0x958          @ Decay
    .hword  0x40, 0x57C, 0x98C
    .hword  0x62, 0x26C, 0x67C          @ Pan
    .hword  0

.align 2
env_src_names:                          @ the eight stock names, then ENV
    .word   0x080CEA68, 0x080CEA94, 0x080CE644, 0x080CEA70
    .word   0x080CEA78, 0x080CEA80, 0x080CEA88, 0x080CEA90
    .word   s_env
env_src_codes:
    .hword  0, 0xD, 4, 0x19, 0x1A, 0x1C, 0x1D, 0x8000, ENV_CODE
.align 2
s_env: .asciz "ENV"
.align 2
.ltorg
