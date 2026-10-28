@ Song DJ row stubs (3.1.T). Logic is in dj.c; state is at SRAM4 0x38007400 (DjState).
.equ DJ_SAVE,         0x38007404    @ DjState.save: hook_dj_step0's lr (audio task only)
.equ PLAYER_STEP,     0x0805CDA1    @ FUN_0805CDA0(player, transport, step, ctx, [sp] arg): the step's events

.global hook_dj_step
.global hook_dj_step0
.global mx_eq_thunk

@ 0x0805DD34 in Player_Tick replaces `ldr.w r4, [fp, #0x20]` (the current step; stock adds 1 next and
@ compares it with the step count in r7). fp = player. dj_seq_step reads this step's tick and length
@ from Player_Tick's frame, which is at our sp + 88 after the pushes. Everything but r4 is kept,
@ including the caller-saved VFP registers.
.thumb_func
hook_dj_step:
    push    {r0, r1, r2, r3, r12, lr}
    vpush   {d0-d7}
    mov     r0, fp
    ldr.w   r1, [fp, #0x20]
    mov     r2, r7
    add     r3, sp, #88
    bl      dj_seq_step
    mov     r4, r0
    vpop    {d0-d7}
    pop     {r0, r1, r2, r3, r12, pc}

@ 0x0805DDDC in Player_Tick: `bl FUN_0805CDA0` (the step's events) with a stack argument at [sp], so
@ nothing is pushed before the call. Afterwards dj_seq_done gives the song player its song flag back
@ (dj_seq_step took it away for a step the song was moved to). fp = player is callee-saved; stock
@ reloads every other register it needs after this call.
.thumb_func
hook_dj_step0:
    ldr     r12, =DJ_SAVE
    str     lr, [r12]
    ldr     r12, =PLAYER_STEP
    blx     r12
    mov     r0, fp
    bl      dj_seq_done
    ldr     r12, =DJ_SAVE
    ldr     lr, [r12]
    bx      lr

@ 0x08053522 in the master render replaces `mov r1, r8; ldr r3, [r3, #0xC]` before `blx r3`, the
@ master EQ node's process: mx_eq (mixer.c) runs it without its next stage, the DJ FX. r0 = the
@ node, r8 = SE. We return past the `blx r3`; stock reloads every register it needs after it.
.thumb_func
mx_eq_thunk:
    push    {r4, lr}
    mov     r1, r8
    bl      mx_eq
    pop     {r4, lr}
    add.w   lr, lr, #2
    bx      lr
.ltorg
