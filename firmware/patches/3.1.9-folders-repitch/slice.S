@ 3.1.Y: raise Clip/Slicer slice cap from 128 to 2048.
@
@ Pool header in SRAM4 at 0x38006000 (past folders 0x4930 and Warp 0x38005000).
@ Grown tables in SRAM1+SRAM2 (0x30000000, 256 KB). XML ext scratch in SRAM3.
@ No stock malloc, no FatFs, no DTCM. Grow may run next to audioTask: IRQ off
@ only around the cursor bump and pointer swap.

.equ SL_STATE,    0x38006000
.equ SL_MAGIC,    0x534C4331
.equ SL_CUR,      0x04
.equ SL_LAST,     0x08
.equ SL_SLOTS,    0x10
.equ SL_NMAX,     32
.equ SL_BLOCK,    0x2000
.equ SL_STOCK,    0x200
.equ SL_CAP,      2048
.equ SL_HDR,      0x110
.equ SRAM_POOL,   0x30000000
.equ SRAM_END,    0x30040000
.equ SRAM3,       0x30040000

.equ MEMCPY,      0x080C8D0D
.equ MEMSET,      0x080C8D5F
.equ CELL_ADD,    0x08093CCB
.equ CELL_GET,    0x08093C9F
.equ BAG_SAVE,    0x08095329
.equ ATTR_GET,    0x0804A66D
.equ ATTR_MAKE,   0x0804A6BD
.equ ATTR_SET,    0x0804A365
.equ XML_CHILD,   0x08049EA5
.equ PARAM_GET,   0x08093E9D
.equ PARAM_SET,   0x08093ECD
.equ S_SLICES,    0x080CF1F4
.equ POST_3C,     0x0804C69D
.equ BUSY_WAIT,   0x08077959

.global sl_init
.global hook_engine_cap
.global hook_cell_cap
.global hook_slices_max
.global hook_slice_max
.global hook_save_iter
.global hook_save_bag
.global hook_cell_ext
.global hook_pad_slice
.global hook_pad_done

@ ---- pool

.thumb_func
sl_init:
    push    {r0, r1, r2, lr}
    ldr     r0, =SL_STATE
    movs    r1, #0
    mov.w   r2, #SL_HDR
    call    MEMSET
    ldr     r1, =SL_MAGIC
    ldr     r0, =SL_STATE
    str     r1, [r0]
    pop     {r0, r1, r2, pc}

@ r0 = address of the table pointer word (count at +4). Returns r0 = 1 if writable.
@ Stock 512-byte tables stay put until count hits 128; only then move to SRAM1+2.
.thumb_func
sl_ensure:
    push    {r4, r5, r6, lr}
    mov     r4, r0
    ldr     r1, [r4]
    ldr     r2, =SRAM_POOL
    cmp     r1, r2
    blo     1f
    ldr     r2, =SRAM_END
    cmp     r1, r2
    bhs     1f
    movs    r0, #1
    pop     {r4, r5, r6, pc}
1:
    ldr     r1, [r4, #4]
    cmp     r1, #0x80
    bhs     4f
    movs    r0, #1
    pop     {r4, r5, r6, pc}
4:
    ldr     r0, =SL_STATE
    ldr     r1, [r0]
    ldr     r2, =SL_MAGIC
    cmp     r1, r2
    it      ne
    blne    sl_init
    ldr     r5, [r4]
    bl      sl_alloc
    cbz     r0, 2f
    mov     r6, r0
    cbz     r5, 3f
    mov     r1, r5
    mov.w   r2, #SL_STOCK
    call    MEMCPY
3:
    cpsid   i
    str     r6, [r4]
    cpsie   i
    movs    r0, #1
    pop     {r4, r5, r6, pc}
2:
    movs    r0, #0
    pop     {r4, r5, r6, pc}

@ r4 = owner field. Returns r0 = 8 KB block or 0.
.thumb_func
sl_alloc:
    push    {r5, r6, r7, lr}
    ldr     r5, =SL_STATE
    add     r0, r5, #SL_SLOTS
    movs    r1, #SL_NMAX
1:
    ldr     r2, [r0]
    cbz     r2, 3f
    ldr     r3, [r2]
    ldr     r2, [r0, #4]
    cmp     r3, r2
    bne     3f
    adds    r0, #8
    subs    r1, #1
    bne     1b
    movs    r0, #0
    pop     {r5, r6, r7, pc}
3:
    cpsid   i
    ldr     r2, [r0, #4]
    cbnz    r2, 5f
    ldr     r2, [r5, #SL_CUR]
    ldr     r3, =(SRAM_END - SRAM_POOL)
    add     r6, r2, #SL_BLOCK
    cmp     r6, r3
    bhi     6f
    str     r6, [r5, #SL_CUR]
    ldr     r3, =SRAM_POOL
    add     r2, r3
    str     r2, [r0, #4]
5:
    str     r4, [r0]
    ldr     r0, [r0, #4]
    cpsie   i
    pop     {r5, r6, r7, pc}
6:
    cpsie   i
    movs    r0, #0
    pop     {r5, r6, r7, pc}

@ ---- insert caps

@ Replaces cmp r2, #0x7f / bhi at 0x08068B62. r4 = clip, r2 = count.
.thumb_func
hook_engine_cap:
    push    {r0, r1, r3, lr}
    ldr     r0, =SL_STATE
    str     r4, [r0, #SL_LAST]
    add     r0, r4, #0x24C
    bl      sl_ensure
    cmp     r0, #0
    beq     2f
    ldr     r2, [r4, #0x250]
    pop     {r0, r1, r3, lr}
    ldr     r3, =SL_CAP - 1
    cmp     r2, r3
    bhi     1f
    ldr     r12, =0x08068B67
    bx      r12
1:
    ldr     r12, =0x08068BD9
    bx      r12
2:
    pop     {r0, r1, r3, lr}
    ldr     r12, =0x08068BD9
    bx      r12

@ Replaces cmp r2, #0x7f / bhi at 0x08093CD0. r0 = list, r1 = pos, lr = list.
.thumb_func
hook_cell_cap:
    push    {r0, r1, r3, lr}
    bl      sl_ensure
    cmp     r0, #0
    beq     2f
    pop     {r0, r1, r3, lr}
    ldr     r2, [r0, #4]
    ldr     r3, =SL_CAP - 1
    cmp     r2, r3
    bhi     1f
    ldr     r12, =0x08093CD5
    bx      r12
1:
    ldr     r12, =0x08093D05
    bx      r12
2:
    pop     {r0, r1, r3, lr}
    ldr     r12, =0x08093D05
    bx      r12

@ Replaces movs + str [sp, #4]. bl only sets lr; the caller's stack slot stays [sp, #4].
.thumb_func
hook_slices_max:
    mov.w   r0, #SL_CAP
    str     r0, [sp, #4]
    bx      lr

.thumb_func
hook_slice_max:
    mov.w   r3, #SL_CAP
    str     r3, [sp, #4]
    bx      lr

@ ---- XML save (pcmStreamer)

@ bl replacement of FUN_08093c9e in doc_save. r0=list r1=index r2=out. r8=slices node.
.thumb_func
hook_save_iter:
    cmp     r1, #0x80
    bhs     1f
    ldr     r12, =CELL_GET
    bx      r12
1:
    bne     2f
    ldr     r3, [r0, #4]
    cmp     r3, #0x80
    bls     2f
    push    {r0, r1, r2, r4, r5, r6, r7, lr}
    bl      sl_write_ext
    pop     {r0, r1, r2, r4, r5, r6, r7, lr}
2:
    movs    r0, #0
    bx      lr

@ r0 = list, r8 = slices element. Format table[128..] into SRAM3, set attr ext.
.thumb_func
sl_write_ext:
    push    {r4, r5, r6, r7, r8, lr}
    mov     r7, r0
    ldr     r4, =SRAM3
    ldr     r6, [r7]
    ldr     r7, [r7, #4]
    movs    r5, #0x80
    mov     r0, r4
1:
    cmp     r5, r7
    bhs     3f
    cmp     r5, #0x80
    beq     2f
    movs    r1, #','
    strb    r1, [r0], #1
2:
    ldr     r1, [r6, r5, lsl #2]
    bl      sl_utoa
    adds    r5, #1
    b       1b
3:
    movs    r1, #0
    strb    r1, [r0]
    mov     r0, r8
    adr     r1, s_ext
    call    ATTR_MAKE
    ldr     r1, =SRAM3
    call    ATTR_SET
    pop     {r4, r5, r6, r7, r8, pc}

@ r1 = value, r0 = dest. Returns r0 advanced. Unsigned decimal.
.thumb_func
sl_utoa:
    push    {r2, r3, r4, r5, lr}
    mov     r4, r0
    mov     r2, r1
    movs    r5, #0
1:
    movs    r3, #10
    udiv    r0, r2, r3
    mls     r1, r0, r3, r2
    adds    r1, #0x30
    push    {r1}
    adds    r5, #1
    mov     r2, r0
    cmp     r2, #0
    bne     1b
2:
    pop     {r1}
    strb    r1, [r4], #1
    subs    r5, #1
    bne     2b
    mov     r0, r4
    pop     {r2, r3, r4, r5, pc}

@ bl replacement of FUN_08095328 for pad cells. Clamp actslice/gridcount in XML only.
.thumb_func
hook_save_bag:
    push    {r0, r1, r2, r4, r5, r6, lr}
    mov     r4, r2
    movs    r5, #0
    movs    r6, #0
    mov     r0, r4
    movs    r1, #0x4E
    call    PARAM_GET
    cmp     r0, #0x80
    ble     1f
    mov     r5, r0
    mov     r0, r4
    movs    r1, #0x4E
    movs    r2, #0x80
    call    PARAM_SET
1:
    mov     r0, r4
    mov.w   r1, #0xB5
    call    PARAM_GET
    cmp     r0, #0x80
    ble     2f
    mov     r6, r0
    mov     r0, r4
    mov.w   r1, #0xB5
    movs    r2, #0x80
    call    PARAM_SET
2:
    ldr     r0, [sp]
    ldr     r1, [sp, #4]
    ldr     r2, [sp, #8]
    call    BAG_SAVE
    cbz     r5, 3f
    mov     r0, r4
    movs    r1, #0x4E
    mov     r2, r5
    call    PARAM_SET
3:
    cbz     r6, 4f
    mov     r0, r4
    mov.w   r1, #0xB5
    mov     r2, r6
    call    PARAM_SET
4:
    pop     {r0, r1, r2, r4, r5, r6, pc}

@ ---- pad apply (defaultTask)

@ Pad apply posts one engine 0x3C per cell slice through a 64-deep queue.
@ Flooding it with 645 posts on ATFG corrupts neighboring pad names. Post at
@ most 128 through the stock queue; hook_pad_done then copies the full cell
@ list into the clip table that hook_engine_cap recorded.

@ Replaces bl 0x0804c69c at 0x08099288. r4 = index, r0/r1/r2/r3 = post args.
.thumb_func
hook_pad_slice:
    cmp     r4, #0x80
    bhs     1f
    cbnz    r4, 2f
    push    {r0, r1, r2, r3}
    ldr     r0, =SL_STATE
    movs    r1, #0
    str     r1, [r0, #SL_LAST]
    pop     {r0, r1, r2, r3}
2:
    ldr     r12, =POST_3C
    bx      r12
1:
    bx      lr

@ Replaces cmp r0, #0 / bne loop at 0x08099296. r0 = CELL_GET result; r5 = list.
.thumb_func
hook_pad_done:
    cmp     r0, #0
    bne     1f
    push    {r0, r1, r2, r3, r4, r5, r6, r7, lr}
    bl      sl_pad_sync
    pop     {r0, r1, r2, r3, r4, r5, r6, r7, lr}
    add     sp, #0x20
    pop.w   {r4, r5, r6, r7, r8, pc}
1:
    ldr     r12, =0x0809927F
    bx      r12

@ r5 = cell slice list. Copy its table into the clip saved by hook_engine_cap.
@ Skip when the cell has no slices so a prior pad's clip is not wiped.
@ last_clip is set when audio drains the 0x3C post; wait a few ticks if needed.
.thumb_func
sl_pad_sync:
    push    {r4, r5, r6, r7, lr}
    ldr     r7, [r5, #4]
    cbz     r7, 9f
    ldr     r6, [r5]
    cbz     r6, 9f
    movs    r4, #8
1:
    ldr     r0, =SL_STATE
    ldr     r0, [r0, #SL_LAST]
    cbnz    r0, 2f
    subs    r4, #1
    beq     9f
    movs    r0, #1
    call    BUSY_WAIT
    b       1b
2:
    mov     r4, r0
    add     r0, r4, #0x24C
    bl      sl_ensure
    cbz     r0, 9f
    ldr     r0, [r4, #0x24C]
    mov     r1, r6
    lsls    r2, r7, #2
    call    MEMCPY
    str     r7, [r4, #0x250]
9:
    pop     {r4, r5, r6, r7, pc}

@ ---- XML load (pcmStreamer)

@ Replaces the slice-loop exit in doc_open (ldr r4,[sp,#0xc] / b 0x08096fa0).
@ r4 is still that cell's slice list; [sp,#0xc] is the <cell> node. Stock has
@ already inserted every <slice> tag, so ext positions go on the end.
.thumb_func
hook_cell_ext:
    ldr     r0, [sp, #0xc]
    push    {r0, r4, r5, r6, r7, lr}
    ldr     r6, [sp, #4]
    ldr     r1, =S_SLICES
    call    XML_CHILD
    cbz     r0, 9f
    adr     r1, s_ext
    movs    r2, #0
    call    ATTR_GET
    cbz     r0, 9f
    mov     r1, r0
    mov     r0, r6
    bl      sl_parse_ext
9:
    pop     {r0, r4, r5, r6, r7, lr}
    ldr     r4, [sp, #0xc]
    ldr     r12, =0x08096FA1
    bx      r12

@ r0 = slice list, r1 = ext string
.thumb_func
sl_parse_ext:
    push    {r4, r5, r6, lr}
    mov     r4, r1
    mov     r5, r0
1:
    ldrb    r0, [r4]
    cmp     r0, #0
    beq     4f
    cmp     r0, #','
    bne     2f
    adds    r4, #1
    b       1b
2:
    movs    r6, #0
3:
    ldrb    r0, [r4]
    cmp     r0, #'0'
    blo     5f
    cmp     r0, #'9'
    bhi     5f
    sub     r0, #'0'
    movs    r1, #10
    mul     r6, r1
    add     r6, r0
    adds    r4, #1
    b       3b
5:
    mov     r0, r5
    mov     r1, r6
    call    CELL_ADD
    b       1b
4:
    pop     {r4, r5, r6, pc}

.align 2
s_ext:
    .asciz  "ext"
.align 2
