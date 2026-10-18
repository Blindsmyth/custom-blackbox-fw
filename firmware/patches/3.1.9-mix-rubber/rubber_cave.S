@ 3.1.R = 3.1.Q (folders, Warp, Mix overhaul, Tools > Master, looper) + Warp Rubber.
@ Built with -I ../3.1.9-folders-repitch-mix -I ../3.1.9-folders-repitch.
@ Rubber Band itself lives in flash bank 2 (tools/stretch/rubber); bank 1 holds only the hooks,
@ the pairing check and the card loader (tools/bank2), linked into this cave.
.set WARP_RUBBER, 1                     @ repitch.S: Warp value 2 "Rubber", hook_lane
.include "mix_cave.S"

.align 2
.ltorg

    .syntax unified
    .thumb

@ The card loader's hook (tools/bank2/b2load.c). pcmStreamer's loop FUN_080743B0 calls the
@ file-command step FUN_08092F5C from 0x0807443E, 0x0807446E and 0x080744D8; each of those `bl`
@ comes here, the loader has its look, and the step runs as before with r0-r3 untouched.
.global b2load_thunk
.thumb_func
b2load_thunk:
    push    {r0, r1, r2, r3, r12, lr}
    bl      rb_load_poll
    pop     {r0, r1, r2, r3, r12, lr}
    ldr     r12, =0x08092F5D
    bx      r12
.ltorg

@ Stock's sample allocator (FUN_08044404, top-down from 0x24000088) resets its top to
@ 0xC4000000 when the stack empties (FUN_08044470, `mov.w r2, #0xC4000000` at 0x08044498).
@ The top megabyte belongs to Rubber Band's heap (tools/stretch/rubber/rt.c), so the reset
@ uses 0xC3F00000; the boot value at 0x080F1E08 is patched the same way (build_patch.py).
.global sdram_top_thunk
.thumb_func
sdram_top_thunk:
    ldr     r2, =0xC3F00000
    ldr     r12, =0x0804449D
    bx      r12
.ltorg

@ rb_call_on_stack(fn, a, b, c, d): fn(a, b, c, d) on Rubber's own stack, the top 32 KB of SRAM1-3
@ (tools/stretch/rubber/rubber.h RB_STACK_TOP). Rubber Band's render needs ~5 KB of stack, which
@ the audio task cannot spare on top of the stock render chain it is called from. Thread mode,
@ so this moves the task's PSP; a preemption saves and restores it like any other. Only one task
@ is ever in here: the disk task for rubber_boot, then the audio task once the voice is READY.
.global rb_call_on_stack
.thumb_func
rb_call_on_stack:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldr     r5, [sp, #16]               @ d
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    mov     r3, r5
    mov     r6, sp
    ldr     r5, =0x30048000
    mov     sp, r5
    blx     r4
    mov     sp, r6
    pop     {r4, r5, r6, pc}
.ltorg
