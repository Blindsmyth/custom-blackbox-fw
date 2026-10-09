@ Batch 0: encoders stop navigating (docs/roadmap.md). Selection is by touch, and later by
@ PADS/SEQS held + tap. The freed encoders do nothing until they are remapped.
@
@ - Pads (vtable 0x080F0E50) and Seq (0x080F0E90, in seqtools.S): logical encoders 0, 1, 2
@   move the pad selection (Pads sliders +0x20C0/+0x20F0/+0x20D8, Seq +0x1A98/+0x1AC8/+0x1AB0,
@   each posts 0x63). Encoder 3 switches the side panel and is kept.
@ - Scroll lists (class 0x080B9C84: pad INFO lists, settings lists): the upper encoders
@   select the row through FUN_080B9BA4. A row tap already selects it (event 0x145 ->
@   FUN_080B9C2A), so the encoder path becomes a no-op. The lower encoder still edits the
@   selected row (FUN_080B9A3C).

.equ PADS_EVENT,  0x080B4135

.global pads_on_event
.global list_row_knob

@ r1 = event. Z set if it is a navigation encoder (0x32, logical index 0..2). Clobbers r2.
.macro nav_knob
    ldrh    r2, [r1]
    cmp     r2, #0x32
    bne     1f
    ldrh    r2, [r1, #0xC]
    cmp     r2, #3
    bhs     1f
    bx      lr
1:
.endm

.thumb_func
pads_on_event:
    nav_knob
    ldr     r12, =PADS_EVENT
    bx      r12

@ b.w at FUN_080B9BA4 entry (list, delta): ignore.
.thumb_func
list_row_knob:
    bx      lr

.align 2
.ltorg
