@ Custom (free-length) sequence recording, Ableton-clip style. docs/map-319.md "Sequence recording".
@
@ Params (registered by chaining the 0x2D enum registration at 0x0808C420):
@   0x19D "Rec Len:"     per sequence, layer 0: Custom, 1/8, 1/4, 1/2, 1 bar ... 32 bars, and
@                        10 = the sequence's own length, labelled from RAM ("15/16"). Editing
@                        Step Count / Step Len syncs it (sr_sync): a preset length -> that
@                        preset, anything else -> 10. Custom stays free-length recording.
@                        A fixed value sets the sequence length (0x86, and 0x85 = 1/16 when
@                        the length isn't a whole number of steps and the layer is empty).
@   0x19E "Seq Default:" global (Tools -> Rec): any Rec Len value, default 1 bar. New, loaded
@                        (without their own Rec Len) and cleared sequences take it.
@   0x1A0 "Loop BPM:"    global (Tools -> Rec): tempo range for looper takes, default 80-160.
@   0x1A1 "Dbl Copy:"    global (Tools -> Rec): the length knob's x2 copies notes. Default On.
@   0x19F "Rec Len Set:" per sequence, hidden: 1 once the user changed 0x19D, 0 after CLR.
@ App_SetParam/App_GetParam keep only some sequence ids in layer 0; two b.w patches add
@ 0x19D/0x19F to that set.
@
@ Custom recording (UI task). Eligible = Rec Len Custom, active layer empty, sequence stopped.
@ - REC with the transport stopped: looper-style take (looper.S) on any empty stopped
@   sequence: arm, first note, REC again sets the tempo from the take and starts playback.
@ - Recording on (App_Update 0x66 -> FUN_0809AC60): stock launches the sequence on its next
@   Quant Size (0x46) boundary. We set 0x86 to 512 so nothing wraps and note the start.
@   The real start tick comes from the sequence-started message (0x4E -> FUN_0809D240).
@ - Recording off (REC again, or STOP): before stock finalises hanging notes, set 0x86 so the
@   loop ends on the next Quant Size boundary. The sequence keeps playing and loops there.

@ SR_LEN 0x19D / SR_DEF 0x19E / SR_SET 0x19F are defined in mixui.S (used by seqtools.S first).
.equ GLOBAL_BAG,  0x24028D18            @ App+0x8C90
.equ APP_SETP,    0x0809FA51            @ App_SetParam(app, ref, id, value, 0)
.equ REC_PLAY,    0x0804C8B5            @ (engine, 1, 0, 1): transport + record, with lead-in
.equ TICKS,       0x0804CA69            @ (out u64, engine): transport ticks, UI copy
.equ AC60,        0x0809AC61            @ App: recording on/off (app, msg{+0xC on})
.equ SEQ_STARTED, 0x0809D241            @ App: sequence started/stopped (app, msg, started)
.equ WAKE_BTN,    0x080A16C5            @ App_HandleInput REC press: FUN_080A16C4(app, 0)
.equ SEQ_CLEAR,   0x0809F821            @ CLR (app, ref)
.equ SEQ_CLEARALL, 0x0809843D           @ clear all layers (app, ref)
.equ QUANT_TABLE, 0x080ECC70            @ float beats per 0x46 option
.equ SR_MAX_STEPS, 512

.equ SR_ACTIVE,   48                    @ MX_STATE: Custom recording running
.equ SR_KEY,      52                    @ its sequence key
.equ SR_START,    56                    @ start tick (low word, signed)
.equ SR_QUANT,    60                    @ quant ticks
.equ SR_STARTSET, 64                    @ 1 once the sequence-started message gave the start
.equ SR_TXT,      0x38005700            @ SRAM4, 16 bytes: label of Rec Len 10
.equ SR_UNEVEN,   10                    @ Rec Len value: a length that is no preset
.equ LABELS_GET,  0x0808E279            @ Param_Labels(id)

.global seqrec_register
.global seqrec_global_tpl
.global seqrec_seq_tpl
.global seqrec_set_layer
.global seqrec_get_layer
.global seqrec_setparam
.global seqrec_clear
.global seqrec_clear_all
.global seqrec_rec_press
.global seqrec_recstate
.global seqrec_started
.global sr_eligible
.global sr_fixed_len
.global sr_user_set
.global sr_labels_hook
.global sr_sync

@ Jump to an absolute Thumb address without touching any register.
.macro jmp addr
    push    {r0, r1}
    ldr     r0, =\addr
    str     r0, [sp, #4]
    pop     {r0, pc}
.endm

@ ---- registration and templates

@ bl site 0x0808C420: stock enum registration (r0..r3 + 2 stack args), then ours. r8 = regs.
.thumb_func
seqrec_register:
    push    {r4, r5, r6, r7, r8, lr}
    ldr     r4, [sp, #24]
    ldr     r5, [sp, #28]
    sub     sp, #8
    str     r4, [sp]
    str     r5, [sp, #4]
    call    PARAM_ENUM
    ldr     r0, =SR_TXT             @ Rec Len 10's label, filled when a page shows it
    ldr     r1, =0x002D2D2D         @ "---"
    str     r1, [r0]
    adr     r6, sr_regs
1:
    ldr     r1, [r6]
    cbz     r1, 2f
    ldr     r2, [r6, #4]            @ title
    ldr     r3, [r6, #8]            @ labels
    ldr     r4, [r6, #12]           @ count
    ldr     r5, [r6, #16]           @ xml name
    str     r4, [sp]
    str     r5, [sp, #4]
    mov     r0, r8
    call    PARAM_ENUM
    adds    r6, #20
    b       1b
2:
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, pc}

.align 2
sr_regs:
    .word   SR_LEN, s_sr_len, sr_len_labels, 11, x_sr_len
    .word   SR_DEF, s_sr_def, sr_len_labels, 10, x_sr_def
    .word   SR_SET, s_sr_set, sr_set_labels, 2, x_sr_set
    .word   SR_BPM, s_sr_bpm, sr_bpm_labels, 5, x_sr_bpm
    .word   SR_DBL, s_sr_dbl, sr_dbl_labels, 2, x_sr_dbl
    .word   0x1A2, s_envtrig, sr_envtrig_labels, 2, x_envtrig  @ adsr.S, pad param
    .word   0
sr_len_labels:                          @ Seq Default uses the first ten
    .word   s_custom, s_1_8, s_1_4, s_1_2, s_1bar, s_2bars, s_4bars, s_8bars, s_16bars, s_32bars
    .word   SR_TXT                      @ 10: the sequence's own length ("15/16"), RAM
sr_set_labels:
    .word   s_no, s_yes
sr_bpm_labels:
    .word   s_bpm60, s_bpm70, s_bpm80, s_bpm90, s_bpm100
sr_dbl_labels:
    .word   s_off, s_on
sr_envtrig_labels:
    .word   s_note, s_slice
sr_len_ticks:                           @ Rec Len -> ticks (960 per beat, 4 beats per bar)
    .word   0, 480, 960, 1920, 3840, 7680, 15360, 30720, 61440, 122880

@ bl site 0x080949D2: global bag template, last add (0x19B). Add Seq Default = 1 bar, Loop BPM, Dbl Copy.
.thumb_func
seqrec_global_tpl:
    push    {r4, lr}
    mov     r4, r0
    call    PARAM_ADD
    mov     r0, r4
    movw    r1, #SR_DEF
    movs    r2, #4                  @ 1 bar
    call    PARAM_ADD
    mov     r0, r4
    movw    r1, #SR_BPM
    movs    r2, #2                  @ 80-160
    call    PARAM_ADD
    mov     r0, r4
    movw    r1, #SR_DBL
    movs    r2, #1                  @ On
    call    PARAM_ADD
.ifdef WARP_SONG_DJ
    mov     r0, r4                  @ 3.1.T: Tools -> Clock BPM Project / Global (mixer.c)
    bl      mx_global_tpl
.endif
    pop     {r4, pc}

@ r0 = the Rec Len a new or reset sequence gets: Custom (0) or 1 bar (4), from 0x19E.
.thumb_func
sr_default:
    push    {r4, lr}
    ldr     r0, =GLOBAL_BAG
    movw    r1, #SR_DEF
    call    P_GET
    cmp     r0, #10
    it      hs
    movhs   r0, #4
    pop     {r4, pc}

@ bl site 0x08094426: sequence bag template (case 0x47), last add (0x7A).
.thumb_func
seqrec_seq_tpl:
    push    {r4, lr}
    mov     r4, r0
    call    PARAM_ADD
    bl      sr_default
    mov     r2, r0
    mov     r0, r4
    movw    r1, #SR_LEN
    call    PARAM_ADD
    mov     r0, r4
    movw    r1, #SR_SET
    movs    r2, #0
    call    PARAM_ADD
    pop     {r4, pc}

@ ---- layer-0 routing (b.w sites; r3 = id - 0xB7 already)

@ App_SetParam 0x0809FE98 (id in r5): stock keeps 0xB7..0xC0 per a bit mask.
.thumb_func
seqrec_set_layer:
    cmp     r3, #9
    bhi     1f
    jmp     0x0809FE9D
1:
    push    {r0}
    movw    r0, #SR_LEN
    cmp     r5, r0
    beq     2f
    movw    r0, #SR_SET
    cmp     r5, r0
    beq     2f
    pop     {r0}
    jmp     0x0809FE3D              @ active layer (stock)
2:
    pop     {r0}
    jmp     0x080A0297              @ layer 0

@ App_GetParam 0x08099672 (id in r8).
.thumb_func
seqrec_get_layer:
    cmp     r3, #9
    bhi     1f
    jmp     0x08099677
1:
    push    {r0}
    movw    r0, #SR_LEN
    cmp     r8, r0
    beq     2f
    movw    r0, #SR_SET
    cmp     r8, r0
    beq     2f
    pop     {r0}
    jmp     0x08099649
2:
    pop     {r0}
    jmp     0x08099685

.align 2
.ltorg

@ ---- helpers

@ sr_setp(app, ref, id, value): App_SetParam with the fifth argument 0.
.thumb_func
sr_setp:
    push    {r4, lr}
    sub     sp, #8
    movs    r4, #0
    str     r4, [sp]
    call    APP_SETP
    add     sp, #8
    pop     {r4, pc}

@ sr_getp(app, ref, id) -> r0 value.
.thumb_func
sr_getp:
    push    {r4, lr}
    sub     sp, #8
    movs    r3, #0
    str     r3, [sp]
    mov     r3, sp
    call    GET_PARAM
    ldr     r0, [sp]
    add     sp, #8
    pop     {r4, pc}

@ sr_layers(app r0, ref r1) -> r0 active layer object, r1 layer 0 object, r2 layer index.
.thumb_func
sr_layers:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    movs    r2, #0
    call    SEQ_OBJ
    mov     r6, r0
    movs    r1, #0xC0
    call    P_GET
    uxtb    r2, r0
    push    {r2, r3}
    mov     r0, r4
    mov     r1, r5
    call    SEQ_OBJ
    pop     {r2, r3}
    mov     r1, r6
    pop     {r4, r5, r6, pc}

@ r0 = layer 0 object. Returns r0 = Quant Size (0x46) in ticks.
.thumb_func
sr_quant_ticks:
    push    {r4, lr}
    movs    r1, #0x46
    call    P_GET
    cmp     r0, #13
    it      hs
    movhs   r0, #1                  @ out of range: 1 bar
    ldr     r1, =QUANT_TABLE
    add.w   r1, r1, r0, lsl #2
    vldr    s0, [r1]
    vldr    s1, sr_960
    vmul.f32 s0, s0, s1
    vmov.f32 s1, #0.5
    vadd.f32 s0, s0, s1
    vcvt.s32.f32 s0, s0
    vmov    r0, s0
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    pop     {r4, pc}

.align 2
sr_960: .float 960.0

@ sr_eligible(app) -> r0 = 1 if the selected sequence is Custom, stopped and its active
@ layer is empty.
.thumb_func
sr_eligible:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    add.w   r5, r4, #0x18           @ selected sequence ref
    mov     r1, r5
    movw    r2, #SR_LEN
    bl      sr_getp
    cbnz    r0, 8f
    mov     r0, r4
    mov     r1, r5
    bl      sr_layers
    mov     r6, r0
    mov     r0, r1
    movs    r1, #0xBE               @ play flag
    call    P_GET
    cbnz    r0, 8f
    ldr     r0, [r6, #0x1C]         @ event count
    cbnz    r0, 8f
    movs    r0, #1
    pop     {r4, r5, r6, pc}
8:
    movs    r0, #0
    pop     {r4, r5, r6, pc}

@ r0 = transport ticks (low word, signed). Clobbers r1-r3, r12.
.thumb_func
sr_now:
    push    {r4, lr}
    sub     sp, #8
    mov     r0, sp
    ldr     r1, =ENGINE
    call    TICKS
    ldr     r0, [sp]
    add     sp, #8
    pop     {r4, pc}

@ sr_fixed_len(app, ref, Rec Len value): set the sequence length to that many bars/notes.
.thumb_func
sr_fixed_len:
    push    {r4, r5, r6, r7, r8, lr}
    sub     sp, #8
    mov     r4, r0
    mov     r5, r1
    cmp     r2, #9
    bhi     9f
    adr     r3, sr_len_ticks
    ldr     r6, [r3, r2, lsl #2]    @ ticks wanted
    bl      sr_layers
    mov     r7, r0                  @ active layer
    bl      step_ticks
    mov     r8, r0                  @ ticks per step
    udiv    r0, r6, r8
    mls     r1, r0, r8, r6
    cbnz    r1, 2f                  @ not a whole number of steps
    cmp     r0, #1
    blt     2f
    cmp     r0, #SR_MAX_STEPS
    ble     5f
2:
    ldr     r1, [r7, #0x1C]
    cbnz    r1, 3f
    mov     r0, r4                  @ empty layer: switch to 1/16 steps (240 ticks)
    mov     r1, r5
    movs    r2, #0x85
    movs    r3, #10
    bl      sr_setp
    movs    r1, #240
    udiv    r0, r6, r1
    b       4f
3:
    add.w   r0, r6, r8, lsr #1      @ nearest whole step count
    udiv    r0, r0, r8
4:
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    cmp     r0, #SR_MAX_STEPS
    it      gt
    movgt   r0, #SR_MAX_STEPS
5:
    mov     r3, r0
    mov     r0, r4
    mov     r1, r5
    movs    r2, #0x86
    bl      sr_setp
9:
    add     sp, #8
    pop     {r4, r5, r6, r7, r8, pc}

.align 2
.ltorg

@ ---- hooks

@ bl site 0x080A1BE6: App_HandleUiEvent 0x6E (a param changed in the UI) ->
@ App_SetParam(app, ref, id, value, [sp]). A user change of Rec Len marks the sequence and
@ applies a fixed length.
.thumb_func
seqrec_setparam:
    push    {r0, r1, r2, r3, r4, lr}
    ldr     r4, [sp, #24]
    sub     sp, #8
    str     r4, [sp]
    ldr     r0, [sp, #8]
    ldr     r1, [sp, #12]
    ldr     r2, [sp, #16]
    ldr     r3, [sp, #20]
    call    APP_SETP
    add     sp, #8
    ldr     r1, [sp, #4]
    ldrh    r1, [r1, #4]
    ubfx    r1, r1, #8, #5
    cmp     r1, #1                  @ a sequence
    bne     9f
    ldr     r2, [sp, #8]
    cmp     r2, #0x85               @ Step Len / Step Count: Rec Len follows the length
    beq     5f
    cmp     r2, #0x86
    beq     5f
    movw    r3, #SR_LEN
    cmp     r2, r3
    bne     9f
    ldr     r0, [sp]
    ldr     r1, [sp, #4]
    ldr     r2, [sp, #12]
    bl      sr_marked
    b       9f
5:
    ldr     r0, [sp]
    ldr     r1, [sp, #4]
    movs    r2, #1
    bl      sr_sync
9:
    pop     {r0, r1, r2, r3, r4, pc}

@ sr_sync(app, ref, post): Rec Len follows the length after a Step Count / Step Len edit or
@ a polymetric knob step: a preset length -> that preset, any other -> 10 (own length). An
@ empty Custom sequence with a preset length stays Custom. Refreshes the label of 10 and,
@ with post, the settings page (0x66 rebuilds its rows, which re-copy their labels).
.thumb_func
sr_sync:
    push    {r4, r5, r6, r7, r8, lr}
    sub     sp, #24
    mov     r4, r1
    mov     r8, r2
    mov     r0, r4
    bl      sq_len
    mov     r5, r0                  @ active layer
    mul     r0, r1, r3
    bl      sq_preset_of
    mov     r6, r0                  @ preset or 0
    ldr     r0, =APP
    mov     r1, r4
    movw    r2, #SR_LEN
    bl      sr_getp
    mov     r7, r0                  @ current Rec Len
    cbnz    r7, 1f
    ldr     r0, [r5, #0x1C]
    cbnz    r0, 1f
    cbnz    r6, 8f                  @ empty Custom at a preset length: still Custom
1:
    movs    r3, r6
    it      eq
    moveq   r3, #SR_UNEVEN
    cmp     r3, r7
    beq     8f
    ldr     r0, =APP
    mov     r1, r4
    movw    r2, #SR_LEN
    bl      sr_setp
8:
    mov     r0, r4
    bl      sr_len_text
    cmp     r8, #0
    beq     9f
    movs    r0, #0x66
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
9:
    add     sp, #24
    pop     {r4, r5, r6, r7, r8, pc}

@ sr_len_text(ref): SR_TXT = the sequence's length as count/denominator ("15/16"), or
@ "Nst" for triplet / bar steps and when longer than six characters.
.thumb_func
sr_len_text:
    push    {r4, lr}
    sub     sp, #16
    bl      sq_len
    mov     r4, r1                  @ count
    adr     r0, sq_den
    ldrb    r0, [r0, r2]
    cbz     r0, 1f
    str     r0, [sp]
    ldr     r0, =SR_TXT
    movs    r1, #16
    adr     r2, fmt_frac_len
    mov     r3, r4
    call    SNPRINTF
    cmp     r0, #6
    ble     9f
1:
    ldr     r0, =SR_TXT
    movs    r1, #16
    adr     r2, fmt_steps
    mov     r3, r4
    call    SNPRINTF
9:
    add     sp, #16
    pop     {r4, pc}

@ bl site 0x080A7F20 (settings row build): Param_Labels(id). For Rec Len, fill the label
@ of value 10 for the selected sequence (App+0x18) first.
.thumb_func
sr_labels_hook:
    push    {r0, lr}
    movw    r1, #SR_LEN
    cmp     r0, r1
    bne     1f
    ldr     r0, =APP + 0x18
    ldrh    r1, [r0, #4]
    ubfx    r1, r1, #8, #5
    cmp     r1, #1                  @ only when a sequence is selected
    bne     1f
    bl      sr_len_text
1:
    pop     {r0, lr}
    jmp     LABELS_GET

@ sr_user_set(app, ref, value): the user picked a Rec Len (Seq screen knob): store it, mark
@ the sequence, set the length.
.thumb_func
sr_user_set:
    push    {r0, r1, r2, lr}
    mov     r3, r2
    movw    r2, #SR_LEN
    bl      sr_setp
    pop     {r0, r1, r2, lr}
    b       sr_marked

@ sr_marked(app, ref, value): Rec Len was set by the user: mark it, apply a fixed length.
.thumb_func
sr_marked:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    movw    r2, #SR_SET
    movs    r3, #1
    bl      sr_setp
    cbz     r6, 9f                  @ Custom: length unchanged
    cmp     r6, #SR_UNEVEN
    bhs     9f                      @ own length: unchanged
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    bl      sr_fixed_len
9:
    pop     {r4, r5, r6, pc}

@ r0 = app, r1 = ref: a cleared sequence takes Seq Default again (Rec Len and its length),
@ touched or not, empty or not.
.thumb_func
sr_reset_default:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    ldrh    r2, [r5, #4]
    ubfx    r2, r2, #8, #5
    cmp     r2, #1
    bne     9f
    bl      sr_default
    mov     r3, r0
    mov     r0, r4
    mov     r1, r5
    movw    r2, #SR_LEN
    bl      sr_setp                 @ (seqrec_setparam sets the length)
    mov     r0, r4
    mov     r1, r5
    movw    r2, #SR_SET
    movs    r3, #0
    bl      sr_setp
9:
    pop     {r4, r5, r6, pc}

@ bl site 0x080A1C8A: CLR.
.thumb_func
seqrec_clear:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    call    SEQ_CLEAR
    mov     r0, r4
    mov     r1, r5
    bl      sr_reset_default
    pop     {r4, r5, r6, pc}

@ bl site 0x080A1C92: clear all layers.
.thumb_func
seqrec_clear_all:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    call    SEQ_CLEARALL
    mov     r0, r4
    mov     r1, r5
    bl      sr_reset_default
    pop     {r4, r5, r6, pc}

.align 2
.ltorg

@ bl site 0x080A2F3C: REC press in App_HandleInput (was FUN_080A16C4(app, 0)). With the
@ transport stopped, REC drives the looper (arm / cancel / finish, looper.S); with it
@ running, stock REC toggles recording and Custom closes on the Quant Size grid.
.thumb_func
seqrec_rec_press:
    push    {r4, lr}
    mov     r4, r0
    call    WAKE_BTN
    ldr     r1, =ENGINE + 0x9A74
    ldr     r1, [r1]                @ sequencer engine
    ldrb    r1, [r1, #0x38]         @ playing
    cbnz    r1, 9f
    mov     r0, r4                  @ stopped: looper-style take (looper.S)
    bl      looper_rec
9:
    pop     {r4, pc}

@ bl site 0x080A282E: recording on/off reaches the UI (app r0, msg r1, msg+0xC = on).
.thumb_func
seqrec_recstate:
    push    {r4, r5, r6, r7, lr}
    sub     sp, #28
    mov     r4, r0
    mov     r5, r1
    ldr     r6, [r5, #0xC]
    cmp     r6, #0
    beq.w   5f
    bl      sr_eligible             @ before stock: it marks the sequence playing
    mov     r7, r0
    mov     r0, r4
    mov     r1, r5
    call    AC60
    cmp     r7, #0
    beq.w   9f
    add.w   r0, r4, #0x8000
    ldrb.w  r0, [r0, #0xCC1]
    cmp     r0, #1                  @ sequence recording (2 = sample recording)
    bne     9f
    mov     r0, r4
    add.w   r1, r4, #0x18
    bl      sr_layers
    mov     r0, r1
    bl      sr_quant_ticks
    mov     r7, r0                  @ q
    bl      sr_now
    cmp     r0, #0                  @ next boundary at or after now
    ble     1f
    add     r0, r7
    subs    r0, #1
1:
    sdiv    r0, r0, r7
    mul     r6, r0, r7
    bl      mx_state
    str     r6, [r0, #SR_START]
    str     r7, [r0, #SR_QUANT]
    add.w   r1, r4, #0x1C
    ldrh    r1, [r1]
    str     r1, [r0, #SR_KEY]
    movs    r1, #0
    str     r1, [r0, #SR_STARTSET]
    movs    r1, #1
    str     r1, [r0, #SR_ACTIVE]
    mov     r0, r4
    add.w   r1, r4, #0x18
    movs    r2, #0x86
    mov     r3, #SR_MAX_STEPS
    bl      sr_setp
    b       9f
5:
    bl      mx_state
    ldr     r1, [r0, #SR_ACTIVE]
    cmp     r1, #0
    beq.w   8f
    movs    r1, #0
    str     r1, [r0, #SR_ACTIVE]
    ldr     r6, [r0, #SR_START]
    ldr     r7, [r0, #SR_QUANT]
    ldr     r1, [r0, #SR_KEY]
    ldr     r2, =REF_TAG
    str     r2, [sp, #16]
    strh    r1, [sp, #20]           @ ref of the recorded sequence
    bl      sr_now
    subs    r0, r0, r6              @ elapsed
    add     r0, r7
    subs    r0, #1
    sdiv    r0, r0, r7              @ ceil(elapsed / q)
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    mul     r6, r0, r7              @ loop length in ticks
    mov     r0, r4
    add     r1, sp, #16
    bl      sr_layers
    bl      step_ticks
    udiv    r3, r6, r0
    cmp     r3, #1
    it      lt
    movlt   r3, #1
    cmp     r3, #SR_MAX_STEPS
    it      gt
    movgt   r3, #SR_MAX_STEPS
    mov     r0, r4
    add     r1, sp, #16
    movs    r2, #0x86
    bl      sr_setp                 @ before stock finalises hanging notes at 0x86
8:
    mov     r0, r4
    mov     r1, r5
    call    AC60
9:
    add     sp, #28
    pop     {r4, r5, r6, r7, pc}

@ bl site 0x080A2696: sequence started (app, msg{+8 engine key}, 1). The first start of the
@ sequence we're recording gives the exact start tick (it starts on a quant boundary).
.thumb_func
seqrec_started:
    push    {r4, r5, r6, lr}
    mov     r5, r1
    call    SEQ_STARTED
    bl      mx_state
    mov     r4, r0
    ldr     r1, [r4, #SR_ACTIVE]
    cbz     r1, 9f
    ldr     r1, [r4, #SR_STARTSET]
    cbnz    r1, 9f
    ldr     r1, [r5, #8]            @ engine key kind<<16 | row<<8 | col
    and     r2, r1, #0xFF
    ubfx    r3, r1, #8, #8
    orr     r2, r2, r3, lsl #4
    lsrs    r3, r1, #16
    orr     r2, r2, r3, lsl #8      @ UI key col | row<<4 | kind<<8
    ldr     r3, [r4, #SR_KEY]
    eors    r2, r3
    movw    r3, #0x1FFF
    tst     r2, r3
    bne     9f
    bl      sr_now
    ldr     r1, [r4, #SR_QUANT]
    cmp     r0, #0
    it      lt
    movlt   r0, #0
    udiv    r0, r0, r1
    mul     r0, r0, r1              @ the boundary it started on
    str     r0, [r4, #SR_START]
    movs    r0, #1
    str     r0, [r4, #SR_STARTSET]
9:
    pop     {r4, r5, r6, pc}

.align 2
s_sr_len:   .asciz "Rec Len:"
s_sr_def:   .asciz "Seq Default:"
s_sr_set:   .asciz "Rec Len Set:"
x_sr_len:   .asciz "seqreclen"
x_sr_def:   .asciz "seqdefault"
x_sr_set:   .asciz "seqreclenset"
s_custom:   .asciz "Custom"
s_1_8:      .asciz "1/8"
s_1_4:      .asciz "1/4"
s_1_2:      .asciz "1/2"
s_1bar:     .asciz "1 bar"
s_2bars:    .asciz "2 bars"
s_4bars:    .asciz "4 bars"
s_8bars:    .asciz "8 bars"
s_16bars:   .asciz "16 bars"
s_32bars:   .asciz "32 bars"
s_no:       .asciz "No"
s_yes:      .asciz "Yes"
s_off:      .asciz "Off"
s_on:       .asciz "On"
s_sr_bpm:   .asciz "Loop BPM:"
s_sr_dbl:   .asciz "Dbl Copy:"
s_envtrig:  .asciz "Env Trig:"
x_envtrig:  .asciz "envtrig"
s_note:     .asciz "Note"
s_slice:    .asciz "Slice"
x_sr_bpm:   .asciz "seqloopbpm"
x_sr_dbl:   .asciz "seqdblcopy"
s_bpm60:    .asciz "60-120"
s_bpm70:    .asciz "70-140"
s_bpm80:    .asciz "80-160"
s_bpm90:    .asciz "90-180"
s_bpm100:   .asciz "100-200"
.align 2
.ltorg
