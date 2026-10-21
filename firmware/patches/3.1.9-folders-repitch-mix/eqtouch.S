@ Batch 1: EQ. Touch a band's dot and drag it right away.
@ Stock drags whichever band is active (+0x1B44) and changes the band only with INFO
@ (event 0x164). Our graph touchDown (vtable 0x080F065C +0x10) picks the dot nearest the
@ finger, makes it active (FUN_080AA03C, so this same drag moves it) and posts the same
@ 0x6E eqactband (0x12E) the INFO path posts, then runs stock touchDown.
@ Dots: graph+0x19D0 + i*0x34, rect at +4 (x, y, w, h). Band key: graph+0x1B98.
@ Default band types (L Shelf, Param, Param, H Shelf) are byte patches in build_patch.py.

.equ EQ_DOWN,     0x080A9B41
.equ EQ_SETBAND,  0x080AA03D
.equ EQ_DOTS,     0x19D0
.equ EQ_ACTIVE,   0x1B44
.equ EQ_KEY,      0x1B98

.global eq_touch_down

@ touchDown(graph, point)
.thumb_func
eq_touch_down:
    push    {r4, r5, r6, r7, r8, r9, r10, lr}
    sub     sp, #32
    mov     r4, r0
    mov     r5, r1
    ldr     r8, [r5]                @ touch x
    ldr     r9, [r5, #4]            @ touch y
    mov     r6, #-1                 @ best band
    mvn     r10, #0x80000000        @ best distance
    movs    r7, #0
1:
    movw    r0, #0x34
    mul     r0, r7, r0
    movw    r1, #EQ_DOTS
    add     r0, r1
    add     r0, r4
    ldr     r1, [r0, #4]            @ x
    ldr     r2, [r0, #12]           @ w
    add     r1, r1, r2, asr #1
    subs    r1, r8, r1
    ldr     r2, [r0, #8]            @ y
    ldr     r3, [r0, #16]           @ h
    add     r2, r2, r3, asr #1
    subs    r2, r9, r2
    mul     r1, r1, r1
    mla     r1, r2, r2, r1
    cmp     r1, r10
    bge     2f
    mov     r10, r1
    mov     r6, r7
2:
    adds    r7, #1
    cmp     r7, #4
    blo     1b
    add.w   r0, r4, #0x1000
    ldr.w   r0, [r0, #(EQ_ACTIVE - 0x1000)]
    cmp     r0, r6
    beq     3f
    mov     r0, r4
    mov     r1, r6
    call    EQ_SETBAND
    movs    r0, #0x6E
    strh    r0, [sp]
    ldr     r0, =EV_TAG
    str     r0, [sp, #4]
    add.w   r0, r4, #0x1000
    ldrh.w  r0, [r0, #(EQ_KEY - 0x1000)]
    strh    r0, [sp, #8]
    movw    r0, #0x12E
    str     r0, [sp, #12]
    str     r6, [sp, #16]
    movs    r0, #0
    str     r0, [sp, #20]
    mov     r0, r4
    mov     r1, sp
    call    UI_POST_UP
3:
    mov     r0, r4
    mov     r1, r5
    add     sp, #32
    pop     {r4, r5, r6, r7, r8, r9, r10, lr}
    ldr     r12, =EQ_DOWN
    bx      r12

.align 2
.ltorg
