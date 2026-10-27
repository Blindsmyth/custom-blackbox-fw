@ 3.1.B = 3.1.Q (folders, Warp, Mix overhaul, Tools > Master, looper) + Warp Bungee (3.1.R's machinery).
@ Built with -I ../3.1.9-folders-repitch-mix -I ../3.1.9-folders-repitch.
@ Bungee itself lives in flash bank 2 (tools/stretch/bungee); bank 1 holds only the hooks,
@ the pairing check and the card loader (tools/bank2), linked into this cave.
.set WARP_RUBBER, 1
#ifdef WITH_REPITCH
.set WARP_KEEP_REPITCH, 1               @ build_patch.py --with-repitch: Stretch / Repitch / Bungee
#endif                     @ repitch.S: Warp Stretch/Rubber only (value 1), hook_lane
#ifdef WITH_LINEUP
.set WARP_LINEUP, 1                     @ build_patch.py --lineup (3.1.T): Stutter / Fill / Repitch / Slice / Grain / Bungee
#endif
#ifdef WITH_DECK
.set WARP_DECK, 1                       @ 3.1.T: Clip Deck (../3.1.9-clip-deck deck.S, deck.c)
.set ID_DECK, DECK_ID_BASE
#endif
#ifdef WITH_SONG_DJ
.set WARP_SONG_DJ, 1                    @ 3.1.T: Song DJ mixer (mixer.c): BPM setting
#ifndef MX_SAFE
.set WARP_SONG_DJ_RING, 1               @ the audio loop's SDRAM ring at boot (left out by --mx-safe)
#endif
#endif
#ifdef WITH_SONG
.set WARP_SONG, 1                       @ 3.1.T: Song cues (deck.c song_*, deck.S hook_song_step, hook_pad_start)
#endif
.include "mix_cave.S"
#ifdef WITH_SONG_DJ
.include "dj.S"                         @ 3.1.T: Song DJ row (../3.1.9-song-dj dj.S, dj.c)
#endif

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

@ rb_call_at(fn, stack_top): fn() on the stack that ends at stack_top. The leaf's background task
@ (tools/bank2/b2pair.c rb_task) runs the leaf's work there, below the part rb_call_on_stack uses,
@ so the audio task preempting it never writes over its frames.
.global rb_call_at
.thumb_func
rb_call_at:
    push    {r4, r6, r7, lr}
    mov     r4, r0
    mov     r6, sp
    mov     sp, r1
    blx     r4
    mov     sp, r6
    pop     {r4, r6, r7, pc}

@ Tools refresh FUN_080C5180, its free-space query (bl 0x0809BBF8 at 0x080C5264): first fill the
@ bar's middle label with the CPU load or the last crash (tools/diag/diag.c cpu_mid). r7 is the
@ screen; the free-space label is r7 + 0x174 and the middle one sits 0xA0 before it.
.global tools_thunk
.thumb_func
tools_thunk:
    push    {r0, r1, r2, r3, r12, lr}
    add.w   r0, r7, #0xD4
    bl      cpu_mid
    pop     {r0, r1, r2, r3, r12, lr}
    ldr     r12, =0x0809BBF9
    bx      r12
.ltorg
