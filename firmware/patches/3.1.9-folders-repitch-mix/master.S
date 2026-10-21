@ Tools -> Master (3.1.Q): the stock master compressor and its limiter, with their settings exposed.
@ docs/map-319.md "Master compressor".
@
@ Stock FUN_0804EDDC (object at SE+0xFC40, called from the master render at 0x08053CA0 only
@ while Compressor 0xB4 is on) runs on hardcoded settings set by its ctor FUN_0804F010:
@   +0x428 threshold dB (-4)   +0x42C ratio (4)   +0x430 input gain dB (-4)
@   +0x448 / +0x44C compressor attack / release, samples (480 / 12000)
@   +0x468 / +0x46C limiter attack / release, samples (192 / 36000). Ceiling: 0 dBFS literal.
@ 3.1.9 registers ids for these (0x12F-0x13B, "C Attack:" ...) but nothing reads them. We use
@ five of them, register two new ones for the limiter, keep all seven in the global bag
@ (settings.tml, like Compressor) and list them on a fifth Tools page, Master.
@
@   0xB4  Compressor: (stock, moved here from Tools -> Main)
@   0x131 C Thresh:   mdB, -48000..0, default -4000
@   0x132 C Ratio:    n:1, 1..100, default 4
@   0x12F C Attack:   ms, 0..10000, default 10
@   0x130 C Release:  ms, 0..10000, default 250
@   0x13A C Gain:     mdB, -36000..36000, default -4000 (stock input gain, before detection)
@   0x1A8 L Attack:   ms, 0..10000, default 4    (new)
@   0x1A9 L Release:  ms, 0..10000, default 750  (new)
@
@ The defaults are the ctor's values, so an untouched Master page sounds exactly like stock.
@ master_comp runs on the audio task: it only reads the global bag and writes the compressor
@ object, then tail-calls stock. No new DSP, no RAM of its own.

.equ COMP_PROC,   0x0804EDDD            @ FUN_0804EDDC(comp, mixer)
.equ REG_INT,     0x0808C12D            @ Param_RegisterInt(reg, id, fmt, title, min, max, xml)
.equ BAG_ADD,     0x08093EF7            @ bag add (bag, id, value)
.equ M_LATK,      0x1A8
.equ M_LREL,      0x1A9
.equ C_THR,       0x428
.equ C_RATIO,     0x42C
.equ C_GAIN,      0x430
.equ C_ATK,       0x448
.equ C_REL,       0x44C
.equ L_ATK,       0x468
.equ L_REL,       0x46C
.equ SR_KHZ,      48                    @ samples per ms

.global master_register
.global master_tpl
.global master_comp
.global master_find
.global s_master

@ ---- registration

@ bl site 0x0808DD3E: Param_RegisterInt(reg, 0x13B, ...) with 3 stack args, then ours.
.thumb_func
master_register:
    push    {r4, r5, r6, lr}
    sub     sp, #16
    ldr     r12, [sp, #32]
    str     r12, [sp]
    ldr     r12, [sp, #36]
    str     r12, [sp, #4]
    ldr     r12, [sp, #40]
    str     r12, [sp, #8]
    mov     r4, r0
    call    REG_INT
    adr     r5, m_regs
1:
    ldr     r1, [r5]
    cbz     r1, 2f
    ldr     r2, [r5, #4]            @ format
    ldr     r3, [r5, #8]            @ title
    ldr     r6, [r5, #12]           @ min
    str     r6, [sp]
    ldr     r6, [r5, #16]           @ max
    str     r6, [sp, #4]
    ldr     r6, [r5, #20]           @ xml name
    str     r6, [sp, #8]
    mov     r0, r4
    call    REG_INT
    adds    r5, #24
    b       1b
2:
    add     sp, #16
    pop     {r4, r5, r6, pc}

@ bl site 0x08094886: global bag template, add(bag, 0xB4, value); then our seven with defaults.
.thumb_func
master_tpl:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    call    BAG_ADD
    adr     r5, m_defaults
1:
    ldr     r1, [r5]
    cbz     r1, 2f
    ldr     r2, [r5, #4]
    mov     r0, r4
    call    BAG_ADD
    adds    r5, #8
    b       1b
2:
    pop     {r4, r5, r6, pc}

@ ---- engine

@ master_find(id) -> r0 value, r1 = 1 if the global bag holds id, else 0. Same walk as the
@ stock getter (entries {u16 id, pad, s32 value} at bag+4, count at bag+0xA), but tells a
@ missing id from a 0 value. Leaf, uses r0-r3 and r12 only.
.thumb_func
master_find:
    ldr     r3, =GLOBAL_BAG
    ldrh    r12, [r3, #0xA]
    ldr     r3, [r3, #4]
    cbz     r3, 8f
    cmp     r12, #0
    beq     8f
1:
    ldrh    r2, [r3]
    cmp     r2, r0
    beq     2f
    adds    r3, #8
    subs    r12, r12, #1
    bne     1b
8:
    movs    r0, #0
    movs    r1, #0
    bx      lr
2:
    ldr     r0, [r3, #4]
    movs    r1, #1
    bx      lr

@ bl site 0x08053CA0: was bl FUN_0804EDDC(comp, mixer). Copies the Master settings into the
@ object (each clamped to its registered range, times to at least one sample), then runs stock.
.thumb_func
master_comp:
    push    {r0, r1, r4, r5, r6, lr}
    mov     r4, r0
    adr     r5, m_fields
1:
    ldrh    r0, [r5]                @ id
    cbz     r0, 9f
    bl      master_find
    cbz     r1, 7f                  @ not in the bag: leave the field as it is
    ldr     r1, [r5, #4]            @ min
    cmp     r0, r1
    it      lt
    movlt   r0, r1
    ldr     r1, [r5, #8]            @ max
    cmp     r0, r1
    it      gt
    movgt   r0, r1
    ldrh    r6, [r5, #2]            @ object offset; bit 15: time in ms -> samples (int)
    tst     r6, #0x8000
    beq     3f
    bic     r6, r6, #0x8000
    movs    r1, #SR_KHZ
    muls    r0, r1, r0
    cmp     r0, #1
    it      lt
    movlt   r0, #1
    str     r0, [r4, r6]
    b       7f
3:
    vmov    s0, r0                  @ float, scaled by m_fields +12
    vcvt.f32.s32 s0, s0
    vldr    s1, [r5, #12]
    vmul.f32 s0, s0, s1
    add     r6, r4
    vstr    s0, [r6]
7:
    adds    r5, #16
    b       1b
9:
    pop     {r0, r1, r4, r5, r6, lr}
    jmp     COMP_PROC

.align 2
@ id, format, title, min, max, xml: the Param_RegisterInt arguments in order
m_regs:
    .word   M_LATK, 0xF, s_l_atk, 0, 10000, x_l_atk
    .word   M_LREL, 0xE, s_l_rel, 0, 10000, x_l_rel
    .word   0
m_defaults:
    .word   0x131, -4000
    .word   0x132, 4
    .word   0x12F, 10
    .word   0x130, 250
    .word   0x13A, -4000
    .word   M_LATK, 4
    .word   M_LREL, 750
    .word   0
@ u16 id, u16 offset (| 0x8000 = ms -> samples), s32 min, s32 max, f32 scale
m_fields:
    .hword  0x131, C_THR
    .word   -48000, 0
    .float  0.001
    .hword  0x132, C_RATIO
    .word   1, 100
    .float  1.0
    .hword  0x13A, C_GAIN
    .word   -36000, 36000
    .float  0.001
    .hword  0x12F, C_ATK | 0x8000
    .word   0, 10000
    .float  0
    .hword  0x130, C_REL | 0x8000
    .word   0, 10000
    .float  0
    .hword  M_LATK, L_ATK | 0x8000
    .word   0, 10000
    .float  0
    .hword  M_LREL, L_REL | 0x8000
    .word   0, 10000
    .float  0
    .hword  0, 0

s_master:   .asciz "Master"
s_l_atk:    .asciz "L Attack:"
s_l_rel:    .asciz "L Release:"
x_l_atk:    .asciz "lattack"
x_l_rel:    .asciz "lrelease"
.align 2
