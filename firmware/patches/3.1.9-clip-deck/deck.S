@ Clip Start/End/Loop stubs. Logic is in deck.c; state is at SRAM4 0x38004930 (DkState).
.equ DK_SAVE,         0x38004934
.equ CLIP_STARTGRAIN, 0x08065551
.equ VOICE_START,     0x08065909
.equ UI_SAVE,         0x38004C98    @ DkState.ui.save[4]: UI-task stubs (the audio task can preempt them)
.equ KEYS_ON,         0x38004C94    @ DkState.ui.keys_on
.equ NOTE_ON,         0x0804C61D
.equ NOTE_OFF,        0x0804C5D5
.equ CUE_SWALLOW,     0x100
.equ WAVE_EVT_CONT,   0x080A66E1

.global hook_note_grain
.global hook_wrap_grain
.global hook_voice
.global hook_keys_on
.global hook_keys_off
.global hook_keys_count
.global hook_beats
.global hook_knob
.global hook_grain_ofs
.global hook_list_value
.global hook_key_lit
.global hook_bag_size
.global hook_limit
.global hook_ui_clip
.global hook_bar
.global hook_wave_evt

@ Clip_StartGrain takes three stack args ([sp], [sp+4], [sp+8]). Nothing here moves sp
@ before the jump: registers go to SRAM, and the C call only uses stack below sp.

@ 0x08065A9A, note start: r3 = first unit (0 on a Clip).
.thumb_func
hook_note_grain:
    ldr     r12, =DK_SAVE
    stmia   r12, {r0, r1, r2, lr}
    mov     r1, r3
    bl      deck_note_unit
    mov     r3, r0
    ldr     r12, =DK_SAVE
    ldmia   r12, {r0, r1, r2, lr}
    ldr     r12, =CLIP_STARTGRAIN
    bx      r12

@ 0x08066CF2, next grain: r3 = next unit (0 after a wrap). The lane render keeps the grain's start
@ tick at its [sp+0x58]; sp here is still the caller's.
.thumb_func
hook_wrap_grain:
    ldr     r12, =DK_SAVE
    stmia   r12, {r0, r1, r2, lr}
    mov     r1, r3
    add     r2, sp, #0x58
    bl      deck_wrap_unit
    mov     r3, r0
    ldr     r12, =DK_SAVE
    ldmia   r12, {r0, r1, r2, lr}
.ifdef MIX_EXT
    @ The combined 3.1.T build: the Mix line's Env Trig hook (adsr.S env_slice) owns this call too;
    @ it starts the grain itself and returns to our caller (neither stub moves sp).
    ldr     r12, =env_slice
    orr     r12, r12, #1
.else
    ldr     r12, =CLIP_STARTGRAIN
.endif
    bx      r12

@ 0x080683AE / 0x08068656 / 0x080686CC in FUN_08067FA4: voice start FUN_08065908(clip, transport,
@ offset, note, [sp] velocity, [sp+4] router). Same no-push rule as the grain stubs. deck_cue_note
@ returns the note to pass on, or CUE_SWALLOW to drop the call (a cue jump is pending instead).
.thumb_func
hook_voice:
    ldr     r12, =DK_SAVE
    stmia   r12, {r0, r1, r2, lr}
    mov     r1, r3
    bl      deck_cue_note
    mov     r3, r0
    ldr     r12, =DK_SAVE
    ldmia   r12, {r0, r1, r2, lr}
    cmp     r3, #CUE_SWALLOW
    beq     1f
    ldr     r12, =VOICE_START
    bx      r12
1:
    bx      lr

@ Keys screen note on / off (0x080A0AA2 / 0x0809B78C): (engine, key, note, 0, [sp] flag). One stack
@ argument, so no push before the jump. deck_keys_note returns the note to pass on or CUE_SWALLOW.
.macro keys_note name, target
.thumb_func
\name:
    ldr     r12, =UI_SAVE
    stmia   r12, {r0, r1, r3, lr}
    mov     r0, r1
    mov     r1, r2
    bl      deck_keys_note
    mov     r2, r0
    ldr     r12, =UI_SAVE
    ldmia   r12, {r0, r1, r3, lr}
    cmp     r2, #CUE_SWALLOW
    beq     1f
    ldr     r12, =\target
    bx      r12
1:
    bx      lr
.endm

    keys_note hook_keys_on, NOTE_ON
    keys_note hook_keys_off, NOTE_OFF

@ 0x080AD9CA replaces `str.w r2, [r3, #0xaa4]` (Keys refresh, Slicer branch: key count = slice count).
@ Cue keys: always 8. The caller pops pc next, so lr is free.
.thumb_func
hook_keys_count:
    ldr     r12, =KEYS_ON
    ldrb    r12, [r12]
    cmp     r12, #0
    it      ne
    movne   r2, #8
    str.w   r2, [r3, #0xaa4]
    bx      lr

@ 0x08064BA2 in Clip_RecalcRates replaces `ldr.w r3, [r4, #0xB80]` (Beat Count, 0 = Auto); stock then
@ tests r3. r4 = clip. Everything else is live, including the caller-saved VFP registers.
.thumb_func
hook_beats:
    push    {r0, r1, r2, r3, r12, lr}
    vpush   {d0-d7}
    mov     r0, r4
    bl      deck_auto_beats
    vpop    {d0-d7}
    str     r0, [sp, #12]
    pop     {r0, r1, r2, r3, r12, pc}

@ 0x080A6E70 in the waveform view's message handler: `bl FUN_080b0d54(knob, delta)` for UI event 0x32
@ (encoder turn). r4 = view. A plain call: deck_knob_turn turns the knob itself or acts on the turn.
.thumb_func
hook_knob:
    mov     r2, r4
    b.w     deck_knob_turn

@ 0x080656E2 in Clip_StartGrain replaces `vmov fp, s15` (the grain's first frame, (int)(unit * spu)).
@ deck_grain_start adds the pad's grid offset. r4 = clip. This stretch keeps the constant 1 in lr for a
@ store a few instructions on (0x080656F2 strh lr), and ip is dead here (rewritten at 0x080656EE), so
@ the stub returns through ip with lr = 1. Pushing is fine: this is inside Clip_StartGrain, after its
@ stack arguments were read relative to its own frame, and sp is back before the return.
.thumb_func
hook_grain_ofs:
    vmov    fp, s15
    push    {r0, r1, r2, r3, ip, lr}
    vpush   {d0-d7}
    mov     r0, r4
    mov     r1, fp
    bl      deck_grain_start
    mov     fp, r0
    vpop    {d0-d7}
    pop     {r0, r1, r2, r3, ip, lr}
    mov     ip, lr
    mov.w   lr, #1
    bx      ip

@ 0x080A9218 in the Info list row's value draw: `bl snprintf(buf, 0x1f, "%d", value)` for number params,
@ r4 = the row. deck_list_value gets the row as a fifth (stack) argument.
.thumb_func
hook_list_value:
    push    {r4, lr}
    sub     sp, #8
    str     r4, [sp]
    bl      deck_list_value
    add     sp, #8
    pop     {r4, pc}

@ 0x080AD728 in the Keys key draw: `bl FUN_080ba3fc(note)` (in the scale?), r4 = the key.
.thumb_func
hook_key_lit:
    mov     r1, r4
    b.w     deck_key_lit

@ 0x08093E7C in FUN_08093E6A (param list alloc) replaces `movs r1,#1; lsls r0,r5,#3`.
@ Stock lists hold 64 rows; a Clip pad template already uses 64, so our ids past
@ 0x1A0 were dropped by Param_Add. Lists asked for 64 get 96. lr is free: the
@ caller pushed it at entry and pops pc.
.thumb_func
hook_bag_size:
    cmp     r5, #0x40
    it      eq
    moveq   r5, #0x60
    movs    r1, #1
    lsls    r0, r5, #3
    bx      lr

@ 0x08066C46 replaces `ldrd r2, r3, [r6, #0x240]`; stock then adds them and compares r8.
.thumb_func
hook_limit:
    push    {r0, r1, r12, lr}
    mov     r0, r6
    mov     r1, r8
    bl      deck_limit
    mov     r3, r0
    movs    r2, #0
    pop     {r0, r1, r12, pc}

@ 0x080A5B0E, Clip case of the waveform update: r0 = wave, r1 = beat list, r4 = view.
.thumb_func
hook_ui_clip:
    mov     r2, r4
    b.w     deck_ui_update

@ 0x080A5568, bottom bar: r0 = view, r1 = is Slicer, r5 = cellmode.
.thumb_func
hook_bar:
    mov     r2, r5
    b.w     deck_bar

@ 0x080A66DC, waveform screen event handler entry (b.w). Replays push/sub.
.thumb_func
hook_wave_evt:
    push    {r0, r1, r2, lr}
    bl      deck_ui_event
    cmp     r0, #0
    pop     {r0, r1, r2, lr}
    bne     1f
    push    {r4, r5, r6, lr}
    sub     sp, #0x28
    ldr     r12, =WAVE_EVT_CONT
    bx      r12
1:
    movs    r0, #0
    bx      lr
.ltorg

.ifdef WARP_SONG
@ Song cues (3.1.T, deck.c song_*). FUN_0805CDA0 entry (b.w): the sequence player's per-step call
@ (r0 player, r1 transport, r2 step, r3 offset, [sp] router). deck.c plans the song's next scene and
@ starts a scene at step 0; then the replayed first instruction and the rest of stock's function.
.equ STEP_CONT, 0x0805CDA5
.global hook_song_step
.thumb_func
hook_song_step:
    push    {r0, r1, r2, r3, r12, lr}
    bl      song_step
    pop     {r0, r1, r2, r3, r12, lr}
    ldrb.w  r1, [r0, #0x58]
    ldr     r12, =STEP_CONT
    bx      r12

@ Clip pad event switch (FUN_08067FA4): its jump-table entry for msg 0x8B (0x0806823C) points here.
@ r4 = the clip, all other registers and the stack are the switch's. song_pad_start returns 1 when
@ the event became a pending cue jump: skip it (the switch's loop, as an unhandled type does).
.equ PAD_8B_STOCK, 0x080683B5
.equ PAD_EVT_NEXT, 0x08067FF5
.global hook_pad_start
.thumb_func
hook_pad_start:
    push    {r0, r1, r2, r3, r12, lr}
    mov     r0, r4
    bl      song_pad_start
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    bne     1f
    ldr     r12, =PAD_8B_STOCK
    bx      r12
1:
    ldr     r12, =PAD_EVT_NEXT
    bx      r12
.ltorg
.endif
