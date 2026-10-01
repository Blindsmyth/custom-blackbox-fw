@ 3.1.M: group folders in the 3.1.9 preset browser.
@
@ State lives in SRAM4 at 0x38000000 (reset by fm_boot each boot). Not DTCM: stock's small-block
@ allocator (0x080443A8) hands out DTCM from 0x20000000 upward. SRAM4 has no stock references and
@ needs no clock enable.
@   +0x000 magic, +0x004 in_load, +0x008 app pointer from the last Load press,
@   +0x100 browse path (folder shown in the list), +0x200 base path (folder of the loaded preset).
@ Both paths start as "Presets", which is the stock behaviour.
@
@ Task rules (docs/map-319.md): FatFs is only called from pcmStreamer (fm_check, via the
@ command dispatcher). UI-task hooks only touch RAM and post commands. Nothing here writes
@ to the card.

.syntax unified
.thumb
.cpu cortex-m7

.equ STATE,       0x38000000
.equ MAGIC,       0x4D464231
.equ S_INLOAD,    0x004
.equ S_APP,       0x008
.equ S_BROWSE,    0x100
.equ S_BASE,      0x200
.equ S_SIZE,      0x300

.equ PM,          0x2401F508
.equ SD,          0x240021A4
.equ ENGINE,      0x2400A9C0
.equ PRESETS,     0x080CEBC4
.equ CMD_CHECK,   0x30
.equ EVT_LOAD,    0x30

.equ BOOT_NEXT,   0x080778D5
.equ LIST_GET,    0x0804290D
.equ LIST_ADD,    0x08042753
.equ SCAN,        0x08042F65
.equ PATH_JOIN,   0x080431A5
.equ STR_CLEAR,   0x0804604B
.equ STR_ASSIGN,  0x0804607D
.equ STR_COPY,    0x0804609F
.equ STR_CSTR,    0x080460BB
.equ RING_ADV,    0x08040BAD
.equ WAKE,        0x0804CA95
.equ LOADBANK,    0x0809E219
.equ SCREEN,      0x0809EAED
.equ BUILDLIST,   0x08091BB9
.equ TRYLOAD,     0x080918C9
.equ XML_PATH,    0x08091861
.equ FSTAT,       0x0808724B
.equ STRCMP,      0x080C8DC5
.equ STRNCPY,     0x080C8B87
.equ STRLEN,      0x080C8E11
.equ MEMSET,      0x080C8D5F

.macro call addr
    ldr r12, =\addr
    blx r12
.endm

.text

@ ---- state

@ Returns r0 = STATE with both paths set to "Presets". Clobbers r1-r3, r12.
.thumb_func
fm_init:
    push {r4, lr}
    ldr r4, =STATE
    mov r0, r4
    movs r1, #0
    mov r2, #S_SIZE
    call MEMSET
    add r0, r4, #S_BROWSE
    bl set_presets
    add r0, r4, #S_BASE
    bl set_presets
    ldr r0, =MAGIC
    str r0, [r4]
    mov r0, r4
    pop {r4, pc}

@ Returns r0 = STATE, initialising it if the magic is missing. Clobbers r1-r3, r12.
.thumb_func
ensure:
    ldr r0, =STATE
    ldr r1, [r0]
    ldr r2, =MAGIC
    cmp r1, r2
    it eq
    bxeq lr
    b fm_init

@ r0 = 256-byte path buffer.
.thumb_func
set_presets:
    ldr r1, =PRESETS
    movs r2, #0xff
    ldr r12, =STRNCPY
    bx r12

@ r0 = path. Returns r0 = 1 when the path is "Presets".
.thumb_func
is_top:
    push {r3, lr}
    ldr r1, =PRESETS
    call STRCMP
    clz r0, r0
    lsrs r0, r0, #5
    pop {r3, pc}

@ Drops the last component of the browse path, never going above "Presets".
@ r0 = path.
.thumb_func
pop_comp:
    push {r4, lr}
    mov r4, r0
    call STRLEN
1:
    subs r0, #1
    ble 2f
    ldrb r1, [r4, r0]
    cmp r1, #0x5C
    bne 1b
    cmp r0, #7
    blt 2f
    movs r1, #0
    strb r1, [r4, r0]
    pop {r4, pc}
2:
    mov r0, r4
    pop {r4, lr}
    b set_presets

@ Appends "\name" to the path if it fits. r0 = path, r1 = name. Returns r0 = 1 on success.
.thumb_func
push_comp:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    call STRLEN
    mov r6, r0
    mov r0, r5
    call STRLEN
    add r0, r6
    cmp r0, #0xFD
    bhi 1f
    adds r0, r4, r6
    movs r1, #0x5C
    strb r1, [r0], #1
    mov r1, r5
    rsb r2, r6, #0xFE
    call STRNCPY
    movs r0, #1
    pop {r4, r5, r6, pc}
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ ---- boot (main, replaces its first call)

.thumb_func
fm_boot:
    push {r0, lr}
    bl fm_init
    pop {r0, lr}
    ldr r12, =BOOT_NEXT
    bx r12

@ ---- path hooks (any task; RAM only)

@ Replaces preset_path_join(dest, pm, comp): the folder is the browse path while a
@ load or check is in progress, otherwise the loaded preset's folder.
.thumb_func
hook_join:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r2
    bl ensure
    ldrb r1, [r0, #S_INLOAD]
    cmp r1, #0
    ite ne
    addne r2, r0, #S_BROWSE
    addeq r2, r0, #S_BASE
    mov r3, r5
    ldr r1, =SD
    mov r0, r4
    call PATH_JOIN
    mov r0, r4
    pop {r4, r5, r6, pc}

@ ---- BuildList hooks (pcmStreamer)

@ Root *.xml scan: only at the top level. The target list was already cleared.
.thumb_func
hook_xml:
    push {r0, r1, r2, r3, r4, lr}
    bl ensure
    add r0, r0, #S_BROWSE
    bl is_top
    cmp r0, #0
    pop {r0, r1, r2, r3, r4, lr}
    beq 1f
    ldr r12, =SCAN
    bx r12
1:
    movs r0, #0
    bx lr

@ Presets directory scan: scan the browse path instead.
.thumb_func
hook_scan:
    push {r0, r2, r3, lr}
    bl ensure
    add r1, r0, #S_BROWSE
    pop {r0, r2, r3, lr}
    ldr r12, =SCAN
    bx r12

@ After the directory rows: add ".." below the top level. Replaces ldr.w r3, [r7, #0x2ac].
@ sb = display list, r7 = PresetMgr.
.thumb_func
hook_dotdot:
    push {r0, r1, r2, r4, r5, lr}
    bl ensure
    add r0, r0, #S_BROWSE
    bl is_top
    cbnz r0, 1f
    mov r0, r9
    ldr r1, =s_dotdot
    call LIST_ADD
1:
    pop {r0, r1, r2, r4, r5, lr}
    ldr.w r3, [r7, #0x2ac]
    bx lr

@ ---- Load hooks (pcmStreamer)

@ Wraps both TryLoad calls in PresetMgr_Load. r0 = pm, r1 = name.
@ On success the browse path becomes the loaded preset's folder.
.thumb_func
hook_tryload:
    push {r4, r5, r6, lr}
    mov r5, r0
    mov r6, r1
    bl ensure
    mov r4, r0
    movs r0, #1
    strb r0, [r4, #S_INLOAD]
    mov r0, r5
    mov r1, r6
    call TRYLOAD
    movs r1, #0
    strb r1, [r4, #S_INLOAD]
    cbz r0, 1f
    mov r5, r0
    add r0, r4, #S_BASE
    add r1, r4, #S_BROWSE
    movs r2, #0xff
    call STRNCPY
    mov r0, r5
1:
    pop {r4, r5, r6, pc}

@ ---- Load press (UI task, RAM only)

@ Replaces SessionMgr_LoadBank(app, idx, 1) in the Load-button case of the app event
@ handler; the following screen switch is skipped by a patched branch. Posts CMD_CHECK
@ with the row name so pcmStreamer can decide between load and navigate.
.thumb_func
hook_ui_load:
    push {r4, r5, r6, r7, lr}
    sub sp, #0x104
    mov r4, r0
    mov r5, r1
    bl ensure
    str r4, [r0, #S_APP]
    ldr r6, =PM
    add r0, r6, #0x32C
    mov r1, r5
    mov r2, sp
    call LIST_GET
    cmp r0, #0
    beq 2f
    movs r3, #0
    strb r3, [r6, #4]
    str r5, [r6]
    strb r3, [r6, #5]
    add r0, r6, #8
    call STR_CLEAR
    add r0, r6, #0xC
    call STR_CLEAR
    add r0, r6, #8
    mov r1, sp
    call STR_ASSIGN
    movs r1, #CMD_CHECK
    strb r1, [r6, #4]
    ldr.w r3, [r6, #0x124]
    and r3, r3, #0xf
    lsls r7, r3, #4
    adds r3, r6, r7
    ldr r2, [r6]
    str r2, [r3, #0x20]
    strb.w r1, [r3, #0x24]
    ldrb r2, [r6, #5]
    strb.w r2, [r3, #0x25]
    add r0, r6, #0x28
    add r0, r7
    add r1, r6, #8
    call STR_COPY
    add r0, r6, #0x2C
    add r0, r7
    add r1, r6, #0xC
    call STR_COPY
    add r0, r6, #0x124
    call RING_ADV
    ldr r0, =ENGINE
    call WAKE
    b 3f
2:
    mov r0, r4
    mov r1, r5
    movs r2, #1
    call LOADBANK
    mov r0, r4
    movs r1, #2
    movs r2, #0
    movs r3, #0
    call SCREEN
3:
    add sp, #0x104
    pop {r4, r5, r6, r7, pc}

@ ---- command dispatcher (pcmStreamer)

@ Replaces ldrb r3, [r4, #0x14]; subs r3, #4 (r4 = PresetMgr). CMD_CHECK is handled here
@ and returns r3 above the jump-table range so the dispatcher returns.
.thumb_func
hook_disp:
    ldrb r3, [r4, #0x14]
    cmp r3, #CMD_CHECK
    beq 1f
    subs r3, #4
    bx lr
1:
    push {r3, lr}
    mov r0, r4
    bl fm_check
    pop {r3, lr}
    movs r3, #(CMD_CHECK - 4)
    bx lr

@ r0 = path, r1 = FILINFO (0x120). Returns FRESULT.
.thumb_func
stat:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r0, r5
    movs r1, #0
    mov r2, #0x120
    call MEMSET
    mov r0, r4
    mov r1, r5
    call FSTAT
    pop {r4, r5, r6, pc}

@ CMD_CHECK: r0 = pm; row name in the command string at +0x18, row index at +0x10.
@   ".."                          -> browse up, rebuild the list
@   <browse>\name\preset.xml      -> post EVT_LOAD (UI runs the stock load)
@   <browse>\name is a directory  -> browse into it, rebuild the list
@   anything else                 -> post EVT_LOAD (stock load handles it, e.g. root xml)
@ Stack: FILINFO at sp, name at sp+0x120, path at sp+0x220.
.thumb_func
fm_check:
    push {r4, r5, r6, r7, r8, lr}
    sub sp, #0x328
    mov r4, r0
    bl ensure
    mov r5, r0
    add r0, r4, #0x18
    call STR_CSTR
    mov r1, r0
    add r0, sp, #0x120
    movs r2, #0xff
    call STRNCPY
    movs r0, #0
    add r1, sp, #0x120
    strb r0, [r1, #0xff]

    add r0, sp, #0x120
    ldr r1, =s_dotdot
    call STRCMP
    cbnz r0, 1f
    add r0, r5, #S_BROWSE
    bl pop_comp
    b 4f
1:
    movs r0, #1
    strb r0, [r5, #S_INLOAD]
    add r0, sp, #0x220
    mov r1, r4
    add r2, sp, #0x120
    call XML_PATH
    movs r0, #0
    strb r0, [r5, #S_INLOAD]
    add r0, sp, #0x220
    mov r1, sp
    bl stat
    cmp r0, #0
    beq 5f

    movs r0, #1
    strb r0, [r5, #S_INLOAD]
    add r0, sp, #0x220
    mov r1, r4
    add r2, sp, #0x120
    bl hook_join
    movs r0, #0
    strb r0, [r5, #S_INLOAD]
    add r0, sp, #0x220
    mov r1, sp
    bl stat
    cmp r0, #0
    bne 5f
    ldrb r0, [sp, #0xC]
    tst r0, #0x10
    beq 5f
    add r0, r5, #S_BROWSE
    add r1, sp, #0x120
    bl push_comp
    cmp r0, #0
    beq 5f
4:
    add r0, r4, #0x394
    call STR_CSTR
    mov r1, r0
    mov r0, r4
    call BUILDLIST
    b 6f
5:
    ldr.w r3, [r4, #0x2ac]
    and r3, r3, #0xf
    add.w r3, r3, r3, lsl #1
    lsls r3, r3, #3
    add.w r3, r3, #0x128
    adds r2, r4, r3
    movs r1, #EVT_LOAD
    strb r1, [r4, r3]
    ldr r1, [r4, #0x10]
    str r1, [r2, #8]
    movs r1, #0
    str r1, [r2, #0xc]
    str r1, [r2, #0x10]
    add r0, r4, #0x2ac
    call RING_ADV
6:
    add sp, #0x328
    pop {r4, r5, r6, r7, r8, pc}

@ ---- UI event pop (UI task, RAM only)

@ Replaces ldrb r3, [r5]; cmp r3, #3 in PresetMgr_PopEvent (r4 = PresetMgr, r5 = event).
@ EVT_LOAD runs the stock Load-button pair: LoadBank(app, idx, 1) and the screen switch.
.thumb_func
hook_pop:
    ldrb r3, [r5]
    cmp r3, #EVT_LOAD
    beq 1f
    cmp r3, #3
    bx lr
1:
    push {r0, r1, r2, r3, r4, lr}
    bl ensure
    ldr r4, [r0, #S_APP]
    cbz r4, 2f
    mov r0, r4
    ldr r1, [r5, #8]
    movs r2, #1
    call LOADBANK
    mov r0, r4
    movs r1, #2
    movs r2, #0
    movs r3, #0
    call SCREEN
2:
    pop {r0, r1, r2, r3, r4, lr}
    movs r3, #EVT_LOAD
    cmp r3, #3
    bx lr

.align 2
s_dotdot:
    .asciz ".."

.align 2
.ltorg
