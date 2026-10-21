@ Batch 0: encoders no longer select pads or sequences, and no longer switch pages on the
@ Pads and Seq screens. Selection is PADS/SEQS held + tap. Parameter menus (scroll lists:
@ one encoder picks the row, the other changes it) stay stock. The freed encoders do nothing
@ until they are remapped.
@
@ - Pads (vtable 0x080F0E50): logical encoders 0, 1, 2 move the pad selection (sliders
@   +0x20C0/+0x20F0/+0x20D8, each posts 0x63) and 3 switches the side panel. All four are off.
@ - Seq: see seqtools.S.

.equ PADS_EVENT,  0x080B4135

.global pads_on_event

.thumb_func
pads_on_event:
    ldrh    r2, [r1]
    cmp     r2, #0x32
    bne     1f
    bx      lr                      @ every Pads encoder: off for now
1:
    ldr     r12, =PADS_EVENT
    bx      r12

.align 2
.ltorg
