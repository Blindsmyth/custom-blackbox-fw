@ Looper-style sequence recording (Boss RC style). docs/firmware/README.md "Sequencer".
@
@ With the transport stopped and the selected sequence's current layer empty and not playing
@ (any Rec Len), REC arms:
@ - The engine only copies played notes into the note ring for a sequence whose player has
@   +0x51 set (player param 0x6E, "record target"; FUN_0805C810 also needs +0x5F, set once
@   DispMode 0xB7 arrives). The engine sets it itself only when recording starts with the
@   transport running (0x08050724), so arming sends msg 0x39 {engine key, 0x6E, 1} to all 16
@   sequence players (a loaded preset may route pad notes to a player other than the UI's
@   selection) plus their DispMode, and finishing / cancelling sends 0x6E = 0 to all.
@ - Pad notes go to the engine's selected sequence (SE+0x29C, global param 0xAF, msg 0x8E at
@   0x0804FF5A) or to MIDI-channel matches (0x0804F8A8). Arming re-sends 0xAF first.
@ - Armed: the REC LED blinks. The first note-on (pad or MIDI) starts the take; REC again
@   before that disarms. PLAY (transport running) disarms too.
@ - Taking: the LED is on. Every note-on/off is buffered with the 48 kHz sample counter
@   (SE+0x00, read when the UI pops the note ring; accuracy = one audio block + one UI loop).
@ - REC again ends the take. Bars in {1, 2, 4, 8, 16}: the fewest that put the tempo inside
@   Tools -> Rec -> Loop BPM (each range is one octave, so one fits unless the take is very
@   short or long). The notes are written quantised (step mode: whole steps; otherwise the
@   Quant Size grid), the length set, the tempo sent (msg 0x20, milli-BPM) and the transport
@   started at tick 0 with the sequence playing (FUN_08098288, msg 0x73 = 0, msg 0x74).
@ Takes under 0.25 s are dropped.

.equ LP_STATE,    80                    @ MX_STATE: 0 idle, 1 armed, 2 taking
.equ LP_T0,       84                    @ sample counter at the first note-on
.equ LP_N,        88                    @ buffered note entries
.equ LP_BUF,      0x38005800            @ SRAM4: LP_MAX x {stamp, note|vel, type|origin<<16}
.equ LP_MAX,      256
.equ LP_MIN_TAKE, 12000                 @ samples (0.25 s)
.equ RING_POP,    0x0804C755            @ (ring, &entry) -> 0 when empty
.equ LED_SET,     0x080436F9            @ (leds, index, on)
.equ LED_OBJ,     0x240021BC
.equ LED_REC,     8
.equ ENG_POST,    0x08051739            @ (SE, msg 24 bytes): engine command ring
.equ TEMPO_UI,    0x08097C1D            @ (app, msg{+0xC milli-BPM}): BPM param 0x84 + refresh
.equ SEQ_LAUNCH,  0x08098289            @ (app, ref): 0xBE = 1 + msg 0x68
.equ APP_HANDLE,  0xE808                @ next event handle

.global looper_pop

@ r0 = SE (sequencer engine).
.thumb_func
lp_se:
    ldr     r0, =ENGINE + 0x9A74
    ldr     r0, [r0]
    bx      lr

@ r0 = SE+0x00 low word (48 kHz sample counter).
.thumb_func
lp_stamp:
    push    {r3, lr}
    bl      lp_se
    ldr     r0, [r0]
    pop     {r3, pc}

@ lp_led(on): REC LED.
.thumb_func
lp_led:
    push    {r4, lr}
    mov     r2, r0
    ldr     r0, =LED_OBJ
    movs    r1, #LED_REC
    call    LED_SET
    pop     {r4, pc}

@ lp_post(id, value at +8): an engine command.
.thumb_func
lp_post:
    push    {r4, r5, lr}
    sub     sp, #28
    mov     r4, r0
    mov     r5, r1
    movs    r0, #0
    str     r0, [sp]
    str     r0, [sp, #4]
    str     r0, [sp, #12]
    str     r0, [sp, #16]
    str     r0, [sp, #20]
    strb    r4, [sp]
    str     r5, [sp, #8]
    bl      lp_se
    mov     r1, sp
    call    ENG_POST
    add     sp, #28
    pop     {r4, r5, pc}

.thumb_func
lp_disarm:
    push    {r3, lr}
    bl      mx_state
    movs    r1, #0
    str     r1, [r0, #LP_STATE]
    movs    r0, #0
    bl      lp_led
    movs    r0, #0
    bl      lp_target
    pop     {r3, pc}

@ lp_engparam(engine key, id, value): msg 0x39 to a sequence player (as FUN_0804C59C).
.thumb_func
lp_engparam:
    push    {r4, lr}
    sub     sp, #24
    movs    r3, #0
    str     r3, [sp]
    str     r3, [sp, #4]
    str     r3, [sp, #20]
    movs    r3, #0x39
    strb    r3, [sp]
    str     r0, [sp, #8]
    str     r1, [sp, #12]
    str     r2, [sp, #16]
    bl      lp_se
    mov     r1, sp
    call    ENG_POST
    add     sp, #24
    pop     {r4, pc}

@ lp_target(on): all 16 sequence players are record targets (param 0x6E = on), so notes
@ reach the ring whichever player the preset routes them to. Arming also re-sends each
@ sequence's DispMode (0xB7), which sets player+0x5F (also required by the ring push).
.thumb_func
lp_target:
    push    {r4, r5, r6, lr}
    sub     sp, #8
    mov     r4, r0
    movs    r5, #0                  @ row << 4 | col
1:
    and     r0, r5, #3
    lsrs    r1, r5, #2
    orr     r6, r0, r1, lsl #8
    orr     r6, r6, #0x10000        @ engine key: kind 1, row, col
    mov     r0, r6
    movs    r1, #0x6E
    mov     r2, r4
    bl      lp_engparam
    cbz     r4, 2f
    ldr     r0, =REF_TAG            @ DispMode of that sequence (layer 0)
    str     r0, [sp]
    and     r0, r5, #3
    lsrs    r1, r5, #2
    orr     r0, r0, r1, lsl #4
    orr     r0, r0, #0x100
    strh    r0, [sp, #4]
    ldr     r0, =APP
    mov     r1, sp
    movs    r2, #0
    call    SEQ_OBJ
    movs    r1, #0xB7
    call    P_GET
    mov     r2, r0
    mov     r0, r6
    movs    r1, #0xB7
    bl      lp_engparam
2:
    adds    r5, #1
    cmp     r5, #16
    blo     1b
    add     sp, #8
    pop     {r4, r5, r6, pc}

@ r0 = app. 1 if the selected sequence's current layer is empty and it isn't playing.
.thumb_func
lp_eligible:
    push    {r4, lr}
    add.w   r1, r0, #0x18
    bl      sr_layers
    ldr     r4, [r0, #0x1C]         @ events
    mov     r0, r1
    movs    r1, #0xBE               @ play flag
    call    P_GET
    orrs    r0, r4
    ite     eq
    moveq   r0, #1
    movne   r0, #0
    pop     {r4, pc}

@ r0 = app. REC with the transport stopped: arm / cancel / finish. Returns r0 = 1 if handled.
.thumb_func
looper_rec:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    bl      mx_state
    mov     r5, r0
    ldr     r1, [r5, #LP_STATE]
    cmp     r1, #1
    beq     1f
    cmp     r1, #2
    beq     2f
    mov     r0, r4                  @ idle: arm if eligible
    bl      lp_eligible
    cbz     r0, 9f
    movs    r0, #1
    str     r0, [r5, #LP_STATE]
    movs    r0, #0
    str     r0, [r5, #LP_N]
    movs    r0, #1
    bl      lp_led
    ldr     r0, =APP + 0x1C         @ the engine's selected sequence = ours (global 0xAF, as
    ldrh    r0, [r0]                @ FUN_08098328): pad notes go to SE+0x29C's player, and a
    and     r2, r0, #0xF            @ loaded preset may not have sent it yet
    ubfx    r1, r0, #4, #4
    orr     r2, r2, r1, lsl #8
    ubfx    r1, r0, #8, #5
    orr     r2, r2, r1, lsl #16
    mov.w   r0, #0x400
    movs    r1, #0xAF
    bl      lp_engparam
    movs    r0, #1                  @ let the engine stream the notes
    bl      lp_target
    b       8f
1:
    bl      lp_disarm               @ armed, no note yet: REC again cancels
    b       8f
2:
    mov     r0, r4
    bl      lp_finish
8:
    movs    r0, #1
9:
    pop     {r4, r5, r6, pc}

@ bl site 0x080A2936 in App_Update: the note-ring pop, once per entry and once more when the
@ ring is empty (that last call is our per-loop tick).
.thumb_func
looper_pop:
    push    {r4, r5, r6, lr}
    mov     r4, r1
    call    RING_POP
    mov     r5, r0
    bl      mx_state
    mov     r6, r0
    ldr     r0, [r6, #LP_STATE]
    cbz     r0, 9f
    cbz     r5, 5f
    ldrb    r0, [r4]
    cmp     r0, #5                  @ note on / off (7, 8 are sequence playback)
    beq     1f
    cmp     r0, #6
    bne     9f
1:
    bl      lp_capture
    b       9f
5:
    bl      lp_tick
9:
    mov     r0, r5
    pop     {r4, r5, r6, pc}

@ r4 = ring entry, r6 = MX_STATE.
.thumb_func
lp_capture:
    push    {r7, lr}
    ldr     r1, [r6, #LP_STATE]
    cmp     r1, #1
    bne     1f
    ldrb    r0, [r4]
    cmp     r0, #5                  @ armed: the first note-on starts the take
    bne     9f
    bl      lp_stamp
    str     r0, [r6, #LP_T0]
    movs    r1, #0
    str     r1, [r6, #LP_N]
    movs    r1, #2
    str     r1, [r6, #LP_STATE]
    movs    r0, #1
    bl      lp_led
1:
    ldr     r7, [r6, #LP_N]
    cmp     r7, #LP_MAX
    bhs     9f
    bl      lp_stamp
    ldr     r1, =LP_BUF
    add     r2, r7, r7, lsl #1
    add     r1, r1, r2, lsl #2
    str     r0, [r1]
    ldr     r0, [r4, #0x10]         @ note | velocity << 16
    str     r0, [r1, #4]
    ldrb    r0, [r4]
    ldrh    r2, [r4, #2]            @ origin
    orr     r0, r0, r2, lsl #16
    str     r0, [r1, #8]
    adds    r7, #1
    str     r7, [r6, #LP_N]
9:
    pop     {r7, pc}

@ r6 = MX_STATE. Armed: blink (256 ms); taking: on. The transport running disarms.
.thumb_func
lp_tick:
    push    {r4, lr}
    bl      lp_se
    ldrb    r0, [r0, #0x38]
    cbz     r0, 1f
    bl      lp_disarm
    b       9f
1:
    ldr     r0, [r6, #LP_STATE]
    movs    r1, #1
    cmp     r0, #2
    beq     2f
    ldr     r1, =UWTICK
    ldr     r1, [r1]
    ubfx    r1, r1, #8, #1
2:
    mov     r0, r1
    bl      lp_led
9:
    pop     {r4, pc}

@ r0 = samples since the first note-on. Returns round(r0 x loop ticks / take). Uses r5 = take,
@ r11 = loop ticks (lp_finish).
.thumb_func
lp_scale:
    vmov    s0, r0
    vcvt.f32.u32 s0, s0
    vmov    s1, r11
    vcvt.f32.u32 s1, s1
    vmul.f32 s0, s0, s1
    vmov    s1, r5
    vcvt.f32.u32 s1, s1
    vdiv.f32 s0, s0, s1
    vmov.f32 s1, #0.5
    vadd.f32 s0, s0, s1
    vcvt.u32.f32 s0, s0
    vmov    r0, s0
    bx      lr

@ lp_finish(app): REC ended the take. Frame: sp+0 event / message (24), +24 t0, +28 0xB7,
@ +32 quant ticks, +36 step mode, +40 step ticks, +44 layer index, +48 milli-BPM, +52 i.
.thumb_func
lp_finish:
    push    {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    sub     sp, #60
    mov     r4, r0
    bl      lp_disarm
    bl      mx_state
    mov     r6, r0
    bl      lp_stamp
    ldr     r1, [r6, #LP_T0]
    str     r1, [sp, #24]
    subs    r5, r0, r1              @ take, samples
    movw    r0, #LP_MIN_TAKE
    cmp     r5, r0
    blo.w   99f
    mov     r0, r4
    bl      lp_eligible
    cmp     r0, #0
    beq.w   99f

    vmov    s0, r5                  @ bars: BPM(1 bar) = 4 beats x 60 x 48000 / take
    vcvt.f32.u32 s0, s0
    vldr    s1, lp_k
    vdiv.f32 s2, s1, s0
    vstr    s2, [sp, #48]
    ldr     r0, =GLOBAL_BAG
    movw    r1, #SR_BPM
    call    P_GET
    cmp     r0, #4
    it      hi
    movhi   r0, #2
    movs    r1, #10
    mul     r0, r0, r1
    adds    r0, #60                 @ low end of the range
    vmov    s3, r0
    vcvt.f32.u32 s3, s3
    vldr    s2, [sp, #48]
    movs    r7, #1
1:
    vcmpe.f32 s2, s3
    vmrs    APSR_nzcv, fpscr
    bge     2f
    cmp     r7, #16
    bhs     2f
    lsls    r7, r7, #1
    vadd.f32 s2, s2, s2
    b       1b
2:
    vldr    s4, lp_40               @ the BPM param's range
    vcmpe.f32 s2, s4
    vmrs    APSR_nzcv, fpscr
    it      lt
    vmovlt.f32 s2, s4
    vldr    s4, lp_250
    vcmpe.f32 s2, s4
    vmrs    APSR_nzcv, fpscr
    it      gt
    vmovgt.f32 s2, s4
    vldr    s4, lp_1000
    vmul.f32 s2, s2, s4
    vmov.f32 s4, #0.5
    vadd.f32 s2, s2, s4
    vcvt.u32.f32 s2, s2
    vstr    s2, [sp, #48]           @ milli-BPM
    mov.w   r0, #3840
    mul     r11, r7, r0             @ loop ticks

    add.w   r8, r4, #0x18           @ selected sequence
    mov     r0, r4
    mov     r1, r8
    bl      sr_layers
    mov     r9, r0                  @ active layer
    mov     r10, r1                 @ layer 0
    str     r2, [sp, #44]
    mov     r0, r9
    bl      step_ticks
    str     r0, [sp, #40]
    mov     r0, r10
    movs    r1, #0x88
    call    P_GET
    str     r0, [sp, #36]
    mov     r0, r10
    movs    r1, #0xB7
    call    P_GET
    str     r0, [sp, #28]
    mov     r0, r10
    bl      sr_quant_ticks
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    str     r0, [sp, #32]
    mov     r0, r4
    mov     r1, r8
    call    SEQ_SNAP                @ one UNDO step

    ldr     r6, [r6, #LP_N]         @ n
    movs    r0, #0
    str     r0, [sp, #52]
3:
    ldr     r10, [sp, #52]          @ i
    cmp     r10, r6
    bhs.w   7f
    add     r0, r10, #1
    str     r0, [sp, #52]
    ldr     r7, =LP_BUF
    add     r0, r10, r10, lsl #1
    add     r7, r7, r0, lsl #2      @ entry i
    ldrb    r0, [r7, #8]
    cmp     r0, #5
    bne     3b
    ldr     r0, [sp, #28]           @ 0xB7 == 0 records plain pad triggers only (as stock)
    cbnz    r0, 4f
    ldrh    r0, [r7, #4]
    cmp     r0, #0
    bne     3b
4:
    mov     r9, r5                  @ off time: the matching note-off, else the end
    add     r1, r10, #1
5:
    cmp     r1, r6
    bhs     6f
    ldr     r2, =LP_BUF
    add     r3, r1, r1, lsl #1
    add     r2, r2, r3, lsl #2
    adds    r1, #1
    ldrb    r3, [r2, #8]
    cmp     r3, #6
    bne     5b
    ldrh    r3, [r2, #10]           @ same origin
    ldrh    r0, [r7, #10]
    cmp     r3, r0
    bne     5b
    ldrh    r3, [r2, #4]            @ same note
    ldrh    r0, [r7, #4]
    cmp     r3, r0
    bne     5b
    ldr     r9, [r2]
    ldr     r0, [sp, #24]
    sub     r9, r9, r0
6:
    ldr     r0, [r7]
    ldr     r1, [sp, #24]
    subs    r0, r0, r1
    bl      lp_scale
    mov     r10, r0                 @ on tick
    mov     r0, r9
    bl      lp_scale
    sub     r9, r0, r10             @ length in ticks
    ldr     r0, [sp, #36]
    cbz     r0, 61f
    ldr     r2, [sp, #40]           @ step mode: whole steps, positions in steps x 960
    add     r0, r10, r2, lsr #1
    udiv    r0, r0, r2
    mul     r1, r0, r2
    cmp     r1, r11
    it      hs
    movhs   r0, #0                  @ the last half step wraps to the start
    mov.w   r1, #960
    mul     r10, r0, r1
    add     r0, r9, r2, lsr #1
    udiv    r0, r0, r2
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    mul     r9, r0, r1
    b       62f
61:
    ldr     r2, [sp, #32]           @ Quant Size grid
    add     r0, r10, r2, lsr #1
    udiv    r0, r0, r2
    mul     r10, r0, r2
    cmp     r10, r11
    it      hs
    subhs   r10, r10, r11
    cmp     r9, #1
    it      lt
    movlt   r9, #1
62:
    ldrh    r0, [r7, #10]           @ event: {1 | origin << 16, pos, len, handle, note|vel, 0}
    ldr     r1, [sp, #28]
    cmp     r1, #2
    it      eq
    moveq   r0, #0x100
    lsls    r0, r0, #16
    orr     r0, r0, #1
    str     r0, [sp]
    str     r10, [sp, #4]
    str     r9, [sp, #8]
    add.w   r0, r4, #0xE000
    ldr.w   r1, [r0, #(APP_HANDLE - 0xE000)]
    str     r1, [sp, #12]
    adds    r1, #1
    str.w   r1, [r0, #(APP_HANDLE - 0xE000)]
    ldr     r0, [r7, #4]
    str     r0, [sp, #16]
    movs    r0, #0
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, r8
    bl      sr_layers
    ldr     r1, [r0, #0x1C]
    cmp     r1, #SR_MAX_STEPS
    bhs.w   3b
    adds    r0, #0x18
    mov     r1, sp
    call    EVL_ADD
    b       3b
7:
    ldr     r0, [sp, #40]           @ length: loop ticks / step ticks
    udiv    r3, r11, r0
    cmp     r3, #1
    it      lt
    movlt   r3, #1
    cmp     r3, #SR_MAX_STEPS
    it      gt
    movgt   r3, #SR_MAX_STEPS
    mov     r0, r4
    mov     r1, r8
    movs    r2, #0x86
    bl      sr_setp
    mov     r0, r4
    mov     r1, r8
    ldr     r2, [sp, #44]
    call    SEQ_PUSH
    add.w   r0, r4, #0x8000
    movs    r1, #1
    strb.w  r1, [r0, #0xCC4]        @ unsaved changes
    mov     r0, r4
    mov     r1, r8
    movs    r2, #1
    bl      sr_sync                 @ Rec Len follows (2 bars -> "2 bars"), page refresh

    movs    r0, #0x20               @ tempo, fractional
    ldr     r1, [sp, #48]
    bl      lp_post
    ldr     r0, [sp, #48]           @ BPM display (param 0x84)
    str     r0, [sp, #12]
    mov     r0, r4
    mov     r1, sp
    call    TEMPO_UI
    mov     r0, r4                  @ the sequence plays from transport start
    mov     r1, r8
    call    SEQ_LAUNCH
    movs    r0, #0x73               @ REC no longer held: no lead-in, no recording
    movs    r1, #0
    bl      lp_post
    movs    r0, #0x74               @ play from tick 0
    movs    r1, #0
    bl      lp_post
99:
    add     sp, #60
    pop     {r4, r5, r6, r7, r8, r9, r10, r11, pc}

.align 2
lp_k:    .float 11520000.0              @ 4 x 60 x 48000
lp_40:   .float 40.0
lp_250:  .float 250.0
lp_1000: .float 1000.0
.align 2
.ltorg
