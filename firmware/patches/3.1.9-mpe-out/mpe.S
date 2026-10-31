@ Seq MIDI out stubs (3.1.U). Logic is in mpe.c; state is at SRAM4 0x38007148 (MpState).
@ docs/mpe-out.md.

.global mpe_param_thunk
.global mpe_out_thunk
.global mpe_msg_thunk
.global mpe_pass_thunk

@ 0x0805EBCE in the player's event loop (FUN_0805E58C, audio task) replaces `ldrh.w r2, [sp, #0x24]`
@ (b.w), right after the 0x39 message's key matched the player. The message is at sp + 0x18:
@ +0xC id, +0x10 value, +0x14 layer. r5 = player, r7 = the router (the list stock's all-off posts
@ to). mpe_param returns 1 for the ids it takes over: stock's switch is skipped and the loop goes
@ on at 0x0805ED38, as after any case. Every register is kept, including the caller-saved VFP
@ ones, and the flags are stock's own (`subs r2, #0x45` next sets them).
.thumb_func
mpe_param_thunk:
    push    {r0, r1, r2, r3, r12, lr}
    vpush   {d0-d7}
    mov     r0, r5
    mov     r1, r7
    add     r2, sp, #(24 + 64 + 0x18)
    bl      mpe_param
    vpop    {d0-d7}
    cmp     r0, #0
    pop     {r0, r1, r2, r3, r12, lr}
    bne     1f
    ldrh.w  r2, [sp, #0x24]
    push    {r0, r1}
    ldr     r0, =0x0805EBD3
    str     r0, [sp, #4]
    pop     {r0, pc}
1:
    push    {r0, r1}
    ldr     r0, =0x0805ED39
    str     r0, [sp, #4]
    pop     {r0, pc}

@ 0x0804D004 in Pcm_PollMidi (FUN_0804CB1C, pcmStreamer): `bl FUN_08044A0E` (mgr, packet, DIN,
@ USB) for engine messages 0x50-0x53. The message is at the caller's sp + 0x48 (+0 type, +8 port
@ byte = player+0x68, +0xC channel, +0x10 note). Stock reloads every register it needs after it.
.thumb_func
mpe_out_thunk:
    push    {r4, lr}
    sub     sp, #8
    add     r4, sp, #(16 + 0x48)
    str     r4, [sp]
    bl      mpe_out
    add     sp, #8
    pop     {r4, pc}

@ 0x0804D018 in Pcm_PollMidi's loop (pcmStreamer) replaces `ldrb.w r3, [sp, #0x48]` (b.w), the type
@ of the message just popped from the MIDI queue into sp + 0x48. mpe_msg(msg, r7) sends a layer's
@ program change on the player's own start / layer messages. Every register is kept, including the
@ caller-saved VFP ones; `adds r5, #1; subs r3, #0x50` next set the flags.
.thumb_func
mpe_msg_thunk:
    push    {r0, r1, r2, r3, r12, lr}
    vpush   {d0-d7}
    add     r0, sp, #(24 + 64 + 0x48)
    mov     r1, r7
    bl      mpe_msg
    vpop    {d0-d7}
    pop     {r0, r1, r2, r3, r12, lr}
    ldrb.w  r3, [sp, #0x48]
    push    {r0, r1}
    ldr     r0, =0x0804D01D
    str     r0, [sp, #4]
    pop     {r0, pc}

@ 0x0805E004 in Player_Tick (audio task): `bl FUN_0804F504`, the post of a pass's 0x4E. After it,
@ mpe_pass(player) sets the stop request for Loop Off. fp = player; stock reloads every other
@ register it needs after this call.
.thumb_func
mpe_pass_thunk:
    push    {r4, lr}
    ldr     r4, =0x0804F505
    blx     r4
    mov     r0, fp
    bl      mpe_pass
    pop     {r4, pc}
.ltorg
