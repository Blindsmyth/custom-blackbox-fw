@ 3.1.Z: 3.1.X folders + Warp (Smooth/Snap/Vari), plus a 2048-slice cap (see slice.S).
@
@ Folder state lives in SRAM4 at 0x38000000 (reset by fm_boot each boot). Not DTCM: stock's
@ small-block allocator (0x080443A8) hands out DTCM from 0x20000000 upward. SRAM4 has no
@ stock references and needs no clock enable.
@   +0x000 magic, +0x004 in_load, +0x008 app pointer, +0x00C Save As dest name,
@   +0x100 browse path, +0x200 base path, +0x300 / +0x400 name buffers,
@   +0x010 mark-cache globals, +0x500 FILINFO + path scratch (so BuildList does not grow the
@   pcmStreamer stack), +0x720 mark cache: NSLOTS folders, least recently used one reused.
@ Both paths start as "Presets", which is the stock behaviour.
@
@ Repitch flags sit at 0x38005000 (past S_SIZE 0x4930) so fm_init's memset does not
@ wipe them. See repitch.S. Slice pool header is at 0x38006000 (slice.S).
@
@ App_Update rebuilds the preset list on a timer even on the pads screen. 3.1.O called f_stat
@ twice per row on every rebuild (4,227 sector reads for 146 rows) on pcmStreamer's 8 KB stack.
@ 3.1.P stats each folder once per browse path, then reuses the cached "/name" marks.
@ The scan lists only directories, so a row is a group when <browse>\name\preset.xml is missing:
@ one f_stat per row. Each f_stat walks from the root, so a first visit to a folder of 88
@ subfolders still starved sample streaming. Now a folder's marks are kept per folder (a slot
@ of name hashes: preset / group / pending), BuildList checks at most MARK_INLINE new rows, and
@ the rest are checked MARK_STEP at a time on idle dispatcher steps (hook_idle), with sample
@ streaming in between. Marks only change the display: Delete and Clean check the card.
@
@ Task rules (docs/map-319.md): FatFs is only called from pcmStreamer (command dispatcher and
@ BuildList). UI-task hooks only touch RAM and post commands. Card writes only come from the stock
@ handlers (Save As, Delete, Rename, New, Clean); the hooks only choose the folder they act on.

.syntax unified
.thumb
.cpu cortex-m7
.fpu fpv5-sp-d16

.equ STATE,       0x38000000
.equ MAGIC,       0x4D464231
.equ S_INLOAD,    0x004
.equ S_APP,       0x008
.equ S_SAVENAME,  0x00C
.equ S_STAMP,     0x010     @ LRU clock
.equ S_CUR,       0x014     @ slot of the folder the last BuildList showed
.equ S_BUDGET,    0x018     @ checks left in this BuildList
.equ S_PEND,      0x01C     @ byte: the shown folder has pending rows (hook_idle runs mark_step)
.equ S_BPEND,     0x01D     @ byte: this BuildList left rows pending
.equ S_FOUND,     0x01E     @ byte: mark_step found a group since the last BuildList
.equ S_SAVEFAIL,  0x01F     @ byte: doc_save failed in this Save / Save As
.equ S_REFUSED,   0x020     @ byte: Save As / New wrote no preset.xml; load_saved answers its Load
.equ S_BROWSE,    0x100
.equ S_BASE,      0x200
.equ S_NAME,      0x300
.equ S_NAME2,     0x400
.equ S_FILINFO,   0x500
.equ S_PATHBUF,   0x620
.equ S_SLOTS,     0x720
.equ SLOT_HASH,   0x0       @ FNV-1a of the browse path
.equ SLOT_STAMP,  0x4       @ 0 = empty
.equ SLOT_COUNT,  0x8
.equ SLOT_ENT,    0x10      @ words: FNV-1a of the row name, low 2 bits = M_*
.equ SLOT_SIZE,   0x800
.equ NSLOTS,      8
.equ NENT,        508       @ (SLOT_SIZE - SLOT_ENT) / 4
.equ M_PRESET,    1
.equ M_GROUP,     2
.equ M_PEND,      3
.equ MARK_INLINE, 4
.equ MARK_STEP,   2
.equ S_SIZE,      0x4930    @ slots end at 0x4720

.equ PM,          0x2401F508
.equ SD,          0x240021A4
.equ ENGINE,      0x2400A9C0
.equ PRESETS,     0x080CEBC4
.equ XML_EXT,     0x080CEBC0
.equ CMD_CHECK,   0x30
.equ EVT_LOAD,    0x30
.equ EVT_DONE,    0x27
.equ EVT_SAVED,   0x26      @ +0x10 != 0: the preset screen shows "<title> Error" (UI event 0x148)
.equ EVT_LOADED,  3         @ +0x10 = success
.equ CMD_LOAD,    0x22
.equ PATH_MAX,    255       @ path_join cuts every path here
.equ BROWSE_MAX,  0xF1      @ path + name when entering: leaves room for "\x\preset.xml"

.equ BOOT_NEXT,   0x080778D5
.equ LIST_GET,    0x0804290D
.equ LIST_ADD,    0x08042753
.equ SCAN,        0x08042F65
.equ PATH_JOIN,   0x080431A5
.equ LEGACY_FMT,  0x08042E79
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
.equ SAVEAS,      0x08091E81
.equ ALS_EXPORT,  0x08091889
.equ RENAME,      0x08092BB9
.equ NEWPRESET,   0x08092C59
.equ DELETE,      0x08092E69
.equ CLEAN,       0x0809283D
.equ UI_POST_UP,  0x080AED15
.equ REQUEST_LIST,0x0809067D
.equ KEY_EXIT,    0x080BDE1F
.equ INPUT_EPI,   0x080A3123
.equ GPIO_READ,   0x0807B349
.equ GPIOC,       0x58020800
.equ FSTAT,       0x0808724B
.equ STRCMP,      0x080C8DC5
.equ STRNCPY,     0x080C8B87
.equ STRLEN,      0x080C8E11
.equ MEMSET,      0x080C8D5F
.equ MEMCPY,      0x080C8D0D
.equ DOC_SAVE,    0x08096755
.equ COPYFILE,    0x08091D81
.equ CUR_NAME,    0x08097AC9      @ (app, buf): the loaded preset's name
.equ KB_SET,      0x080C42AD      @ (keyboard, text)
.equ APP,         0x24020088

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
    ldr r2, =S_SIZE
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

@ Drops the last component of the browse path, never going above "Presets". r0 = path.
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
    b 3f
2:
    mov r0, r4
    bl set_presets
3:
    pop {r4, pc}

@ Appends "\name" to the path if it fits. r0 = path, r1 = name. Returns r0 = 1 on success.
@ The browse path stops at BROWSE_MAX: deeper, no preset.xml path would fit path_join's 255.
.thumb_func
push_comp:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r0, r4
    call STRLEN
    mov r6, r0
    mov r0, r5
    call STRLEN
    add r0, r6
    ldr r1, =STATE + S_BROWSE
    cmp r4, r1
    ite eq
    moveq r1, #BROWSE_MAX
    movne r1, #0xFD
    cmp r0, r1
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

@ Copies name into a 256-byte buffer without a leading '/' (the folder marker).
@ r0 = buffer, r1 = name. Returns r0 = buffer.
.thumb_func
copy_name:
    push {r4, lr}
    mov r4, r0
    ldrb r2, [r1]
    cmp r2, #0x2F
    it eq
    addeq r1, #1
    movs r2, #0xff
    call STRNCPY
    movs r0, #0
    strb r0, [r4, #0xff]
    mov r0, r4
    pop {r4, pc}

@ ---- mark cache (pcmStreamer)

@ FNV-1a. r0 = string. Returns r0 = hash. Clobbers r1-r3.
.thumb_func
fnv:
    ldr r1, =0x811C9DC5
    ldr r3, =0x01000193
1:
    ldrb r2, [r0], #1
    cbz r2, 2f
    eors r1, r2
    muls r1, r3, r1
    b 1b
2:
    mov r0, r1
    bx lr

@ Drops every folder's marks (Save As / Rename / New / Delete change what is on the card).
@ Clobbers r0-r3, r12.
.thumb_func
cache_inval:
    push {r4, lr}
    bl ensure
    mov r4, r0
    movs r1, #0
    str r1, [r4, #S_CUR]
    strb r1, [r4, #S_PEND]
    add r0, r4, #S_SLOTS
    mov r2, #(NSLOTS * SLOT_SIZE)
    call MEMSET
    pop {r4, pc}

@ Start of a BuildList: S_CUR = the browse path's slot, taking the least recently used one when
@ it has none, and a fresh check budget. Clobbers r0-r3, r12.
.thumb_func
mark_begin:
    push {r4, r5, r6, r7, r8, lr}
    bl ensure
    mov r4, r0
    add r0, r4, #S_BROWSE
    bl fnv
    mov r5, r0
    ldr r7, [r4, #S_STAMP]
    adds r7, #1
    it eq
    addeq r7, #1
    str r7, [r4, #S_STAMP]
    add r0, r4, #S_SLOTS
    movs r6, #0
    mov r8, #NSLOTS
1:
    ldr r3, [r0, #SLOT_STAMP]
    cbz r3, 2f
    ldr r2, [r0, #SLOT_HASH]
    cmp r2, r5
    beq 4f
2:
    cbz r6, 3f
    ldr r2, [r6, #SLOT_STAMP]
    cmp r3, r2
    bhs 5f
3:
    mov r6, r0
5:
    add r0, r0, #SLOT_SIZE
    subs r8, #1
    bne 1b
    mov r0, r6
    str r5, [r0, #SLOT_HASH]
    movs r1, #0
    str r1, [r0, #SLOT_COUNT]
4:
    str r7, [r0, #SLOT_STAMP]
    str r0, [r4, #S_CUR]
    movs r1, #MARK_INLINE
    str r1, [r4, #S_BUDGET]
    movs r1, #0
    strb r1, [r4, #S_BPEND]
    pop {r4, r5, r6, r7, r8, pc}

@ r0 = slot, r1 = name hash with the low 2 bits clear. Returns r0 = entry address, or 0.
@ Clobbers r2, r3.
.thumb_func
slot_find:
    ldr r2, [r0, #SLOT_COUNT]
    adds r0, #SLOT_ENT
1:
    cbz r2, 2f
    ldr r3, [r0], #4
    bic r3, r3, #3
    cmp r3, r1
    beq 3f
    subs r2, #1
    b 1b
3:
    subs r0, #4
    bx lr
2:
    movs r0, #0
    bx lr

@ r0 = slot, r1 = entry word. Returns r0 = entry address, or 0 when the slot is full.
@ Clobbers r2, r3.
.thumb_func
slot_add:
    ldr r2, [r0, #SLOT_COUNT]
    cmp r2, #NENT
    bhs 1f
    adds r3, r2, #1
    str r3, [r0, #SLOT_COUNT]
    add r0, r0, #SLOT_ENT
    add r0, r0, r2, lsl #2
    str r1, [r0]
    bx lr
1:
    movs r0, #0
    bx lr

@ r0 = directory row name. Returns r0 = 1 when it is a group folder (no preset.xml). The card is
@ only read for a name this folder's slot does not know yet, while this BuildList has budget
@ left; past that the row shows unmarked and stays pending for mark_step. A row that does not
@ fit a full slot also shows unmarked.
.thumb_func
mark_row:
    push {r4, r5, r6, r7, r8, lr}
    mov r5, r0
    bl ensure
    mov r4, r0
    ldr r6, [r4, #S_CUR]
    cmp r6, #0
    beq 9f
    mov r0, r5
    bl fnv
    bic r7, r0, #3
    mov r0, r6
    mov r1, r7
    bl slot_find
    cbz r0, 1f
    ldr r1, [r0]
    and r1, r1, #3
    cmp r1, #M_PEND
    bne 5f
    mov r6, r0
    b 2f
1:
    mov r0, r6
    orr r1, r7, #M_PEND
    bl slot_add
    cmp r0, #0
    beq 8f
    mov r6, r0
2:
    ldr r1, [r4, #S_BUDGET]
    cmp r1, #0
    beq 7f
    subs r1, #1
    str r1, [r4, #S_BUDGET]
    mov r0, r5
    bl no_xml
    cmp r0, #0
    ite ne
    orrne r1, r7, #M_GROUP
    orreq r1, r7, #M_PRESET
    str r1, [r6]
    pop {r4, r5, r6, r7, r8, pc}
7:
    movs r1, #1
    strb r1, [r4, #S_BPEND]
8:
    movs r0, #0
    pop {r4, r5, r6, r7, r8, pc}
5:
    cmp r1, #M_GROUP
    ite eq
    moveq r0, #1
    movne r0, #0
    pop {r4, r5, r6, r7, r8, pc}
9:
    mov r0, r5
    pop {r4, r5, r6, r7, r8, lr}
    b no_xml

@ One idle dispatcher step (command ring empty, S_PEND set). Checks up to MARK_STEP pending rows
@ of the shown folder; pcmStreamer streams samples between steps. When no row is pending any more
@ and one was a group, rebuilds the list once so the marks show. r0 = pm.
@ Reads the display list's names array ({count, cap, names 0x100 each, ptrs, dirty}) in add
@ order, not list_get: list_get sorts a dirty list, and the UI task sorts it after a rebuild.
.thumb_func
mark_step:
    push {r3, r4, r5, r6, r7, r8, r9, lr}
    mov r8, r0
    bl ensure
    mov r4, r0
    ldr r6, [r4, #S_CUR]
    cmp r6, #0
    beq 8f
    add r0, r4, #S_BROWSE
    bl fnv
    ldr r1, [r6, #SLOT_HASH]
    cmp r0, r1
    bne 8f
    movs r5, #MARK_STEP
    movs r7, #0
1:
    add r0, r8, #0x32C
    ldr r1, [r0]
    cmp r7, r1
    bhs 7f
    ldr r0, [r0, #8]
    add r0, r0, r7, lsl #8
    adds r7, #1
    ldrb r1, [r0]
    cmp r1, #0x2F
    beq 1b
    bl fnv
    bic r1, r0, #3
    mov r0, r6
    bl slot_find
    cmp r0, #0
    beq 1b
    ldr r1, [r0]
    and r1, r1, #3
    cmp r1, #M_PEND
    bne 1b
    cmp r5, #0
    beq 9f
    subs r5, #1
    mov r9, r0
    add r0, r8, #0x32C
    ldr r0, [r0, #8]
    subs r1, r7, #1
    add r0, r0, r1, lsl #8
    bl no_xml
    ldr r1, [r9]
    bic r1, r1, #3
    cbz r0, 2f
    orr r1, r1, #M_GROUP
    movs r0, #1
    strb r0, [r4, #S_FOUND]
    b 3f
2:
    orr r1, r1, #M_PRESET
3:
    str r1, [r9]
    b 1b
7:
    movs r0, #0
    strb r0, [r4, #S_PEND]
    ldrb r0, [r4, #S_FOUND]
    cmp r0, #0
    beq 9f
    add r0, r8, #0x394
    call STR_CSTR
    mov r1, r0
    mov r0, r8
    call BUILDLIST
    b 9f
8:
    movs r0, #0
    strb r0, [r4, #S_PEND]
9:
    pop {r3, r4, r5, r6, r7, r8, r9, pc}

@ Replaces ldr.w r2, [r0, #0x124] at the top of preset_cmd_dispatch (r0 = pm), which pcmStreamer
@ calls on every loop. With the command ring empty and rows pending, runs one mark_step.
.thumb_func
hook_idle:
    ldr r2, =STATE
    ldrb r2, [r2, #S_PEND]
    cbnz r2, 1f
    ldr.w r2, [r0, #0x124]
    bx lr
1:
    ldr.w r2, [r0, #0x124]
    ldr.w r3, [r0, #0x120]
    cmp r2, r3
    it ne
    bxne lr
    push {r0, lr}
    bl mark_step
    pop {r0, lr}
    ldr.w r2, [r0, #0x124]
    bx lr

@ ---- boot (main, replaces its first call)

.thumb_func
fm_boot:
    push {r0, lr}
    bl fm_init
.ifndef WARP_RUBBER
.ifndef WARP_PLAIN
    bl sl_init
.endif
.endif
    pop {r0, lr}
    ldr r12, =BOOT_NEXT
    bx r12

@ ---- path hooks (any task; RAM only)

@ Picks the folder a preset name resolves in: the browse path while a load, check or row command
@ runs, or when comp is the Save As destination buffer; otherwise the loaded preset's folder.
@ r0 = comp. Returns r0 = folder path, r1 = comp without a leading '/'. Clobbers r2, r3, r12.
.thumb_func
pick:
    push {r4, lr}
    mov r4, r0
    bl ensure
    ldrb r1, [r0, #S_INLOAD]
    cbnz r1, 2f
    cbz r4, 1f
    ldr r1, [r0, #S_SAVENAME]
    cmp r1, r4
    beq 2f
1:
    add r0, r0, #S_BASE
    b 3f
2:
    add r0, r0, #S_BROWSE
3:
    mov r1, r4
    cbz r1, 4f
    ldrb r2, [r1]
    cmp r2, #0x2F
    it eq
    addeq r1, #1
4:
    pop {r4, pc}

@ Replaces preset_path_join(dest, pm, comp).
.thumb_func
hook_join:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r0, r2
    bl pick
    mov r2, r0
    mov r3, r1
    ldr r1, =SD
    mov r0, r4
    call PATH_JOIN
    mov r0, r4
    pop {r4, r5, r6, pc}

@ Replaces preset_legacy_xml_path(dest, pm, name), which builds "name.xml" in the card root.
@ Legacy root presets only belong to the top level: when the name resolves in a subfolder the
@ path becomes "..", which FatFs rejects (FF_FS_RPATH = 0), so the stock unlink/rename/load of
@ the root file does nothing.
.thumb_func
hook_legacy:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r0, r2
    bl pick
    mov r5, r1
    bl is_top
    cbz r0, 1f
    mov r0, r4
    ldr r1, =SD
    mov r2, r5
    ldr r3, =XML_EXT
    call LEGACY_FMT
    b 2f
1:
    mov r0, r4
    ldr r1, =s_dotdot
    movs r2, #3
    call STRNCPY
2:
    mov r0, r4
    pop {r4, r5, r6, pc}

@ ---- folder test (pcmStreamer)

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

@ r0 = name (no marker). Returns r0 = 1 when <browse>\name\preset.xml is missing, 0 when it is
@ there or the path does not fit. One f_stat. Uses SRAM4 scratch, not the stack.
.thumb_func
no_xml:
    push {r4, r5, r6, lr}
    mov r5, r0
    bl ensure
    mov r4, r0
    add r0, r4, #S_PATHBUF
    add r1, r4, #S_BROWSE
    bl copy_name
    mov r1, r5
    bl push_comp
    cbz r0, 1f
    add r0, r4, #S_PATHBUF
    ldr r1, =s_presetxml
    bl push_comp
    cbz r0, 1f
    add r0, r4, #S_PATHBUF
    add r1, r4, #S_FILINFO
    bl stat
    cmp r0, #0
    ite ne
    movne r0, #1
    moveq r0, #0
    pop {r4, r5, r6, pc}
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ r0 = name (no marker). Returns r0 = 1 when <browse>\name is a directory without preset.xml.
@ Reads the card every time, never the mark cache: Delete and Clean refuse group folders by this.
.thumb_func
is_group:
    push {r4, r5, r6, lr}
    mov r5, r0
    bl ensure
    mov r4, r0
    add r0, r4, #S_PATHBUF
    add r1, r4, #S_BROWSE
    bl copy_name
    mov r1, r5
    bl push_comp
    cbz r0, 1f
    add r0, r4, #S_PATHBUF
    add r1, r4, #S_FILINFO
    bl stat
    cbnz r0, 1f
    ldrb r0, [r4, #S_FILINFO + 0xC]
    tst r0, #0x10
    beq 1f
    mov r0, r5
    pop {r4, r5, r6, lr}
    b no_xml
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ ---- BuildList hooks (pcmStreamer)

@ Root *.xml scan: only at the top level. The target list was already cleared.
@ This is the start of every BuildList, so it also picks the folder's mark slot.
.thumb_func
hook_xml:
    push {r0, r1, r2, r3, r4, lr}
    bl mark_begin
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

@ Replaces list_add(display list, name) for each directory row: group folders get a leading '/'.
@ r0 = list, r1 = name.
.thumb_func
hook_mark:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r0, r5
    bl mark_row
    cbz r0, 1f
    bl ensure
    add r6, r0, #S_NAME2
    movs r1, #0x2F
    strb r1, [r6]
    add r0, r6, #1
    mov r1, r5
    movs r2, #0xfe
    call STRNCPY
    movs r0, #0
    strb r0, [r6, #0xff]
    mov r5, r6
1:
    mov r0, r4
    mov r1, r5
    call LIST_ADD
    pop {r4, r5, r6, pc}

@ After the directory rows: add ".." below the top level. Replaces ldr.w r3, [r7, #0x2ac].
@ sb = display list, r7 = PresetMgr. Rows this BuildList left pending go to mark_step.
.thumb_func
hook_dotdot:
    push {r0, r1, r2, r4, r5, lr}
    bl ensure
.ifndef MIX_EXT
    add r0, r0, #S_BROWSE                   @ 3.1.Q: no ".." row, the ^ button goes up (pset.S)
    bl is_top
    cbnz r0, 1f
    mov r0, r9
    ldr r1, =s_dotdot
    call LIST_ADD
.endif
1:
    bl ensure
    ldrb r1, [r0, #S_BPEND]
    strb r1, [r0, #S_PEND]
    movs r1, #0
    strb r1, [r0, #S_FOUND]
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

@ ---- row commands (pcmStreamer, command dispatcher)

@ Copies the row name (without marker) to S_NAME and rejects "", ".." and, when r1 != 0, group
@ folders. On success sets in_load so the handler resolves names in the browse folder.
@ r0 = name, r1 = block group folders. Returns r0 = S_NAME, or 0 when rejected.
.thumb_func
row_begin:
    push {r4, r5, r6, lr}
    mov r5, r0
    mov r6, r1
    bl ensure
    mov r4, r0
    add r0, r4, #S_NAME
    mov r1, r5
    bl copy_name
    ldrb r1, [r0]
    cbz r1, 2f
    ldr r1, =s_dotdot
    call STRCMP
    cbz r0, 2f
    cbz r6, 1f
    add r0, r4, #S_NAME
    bl is_group
    cbnz r0, 2f
1:
    movs r0, #1
    strb r0, [r4, #S_INLOAD]
    add r0, r4, #S_NAME
    pop {r4, r5, r6, pc}
2:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ Clears in_load. Preserves r0.
.thumb_func
row_end:
    push {r0, lr}
    bl ensure
    movs r1, #0
    strb r1, [r0, #S_INLOAD]
    pop {r0, pc}

@ Posts a UI event like the stock handlers do. r0 = pm, r1 = event id, r2 = value at +8.
.thumb_func
post_evt:
    push {r4, lr}
    mov r4, r0
    ldr.w r3, [r4, #0x2ac]
    and r3, r3, #0xf
    add.w r3, r3, r3, lsl #1
    lsls r3, r3, #3
    add.w r3, r3, #0x128
    strb r1, [r4, r3]
    adds r3, r4, r3
    str r2, [r3, #8]
    movs r1, #0
    str r1, [r3, #0xc]
    str r1, [r3, #0x10]
    add r0, r4, #0x2ac
    call RING_ADV
    pop {r4, pc}

@ Posts an event with +0x10 = r2 (+8 and +0xC zero). r0 = pm, r1 = event id.
.thumb_func
post_evt_val:
    push {r4, lr}
    mov r4, r0
    ldr.w r3, [r4, #0x2ac]
    and r3, r3, #0xf
    add.w r3, r3, r3, lsl #1
    lsls r3, r3, #3
    add.w r3, r3, #0x128
    strb r1, [r4, r3]
    adds r3, r4, r3
    movs r1, #0
    str r1, [r3, #8]
    str r1, [r3, #0xc]
    str r2, [r3, #0x10]
    add r0, r4, #0x2ac
    call RING_ADV
    pop {r4, pc}

@ r0 = new preset name (no marker). Returns r0 = 1 when <browse>\name\preset.xml fits in
@ PATH_MAX and <browse>\name is not a group folder (saving there would hide its presets).
@ Longer, path_join cuts the paths: Save As made the folder, then copied samples under cut
@ names and wrote no preset.xml (or, before hook_pathjoin, overran the stack and reset).
.thumb_func
save_ok:
    push {r4, r5, r6, lr}
    mov r5, r0
    ldrb r0, [r5]
    cbz r0, 1f
    bl ensure
    add r0, r0, #S_BROWSE
    call STRLEN
    mov r4, r0
    mov r0, r5
    call STRLEN
    add r0, r4
    adds r0, #12
    cmp r0, #PATH_MAX
    bhi 1f
    mov r0, r5
    bl is_group
    eor r0, r0, #1
    pop {r4, r5, r6, pc}
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ Saving wrote no preset.xml: show the stock error and answer the Load the UI posts next
@ (load_saved). r0 = pm. Clobbers r0-r3, r12.
.thumb_func
save_failed:
    push {r4, lr}
    mov r4, r0
    bl ensure
    movs r1, #1
    strb r1, [r0, #S_REFUSED]
    mov r0, r4
    movs r1, #EVT_SAVED
    movs r2, #1
    bl post_evt_val
    pop {r4, pc}

@ Replaces PresetMgr_SaveAs(pm, new, old) for cmd 0x21. The new name goes through a private
@ buffer so hook_join can tell the destination (browse folder) from the sample sources (loaded
@ preset's folder) even when both names are equal. Plain Save (cmd 0x20) is not hooked.
.thumb_func
hook_saveas:
    push {r3, r4, r5, r6, r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl cache_inval
    bl ensure
    mov r7, r0
    movs r0, #0
    strb r0, [r7, #S_SAVEFAIL]
    strb r0, [r7, #S_REFUSED]
    add r0, r7, #S_NAME
    mov r1, r5
    bl copy_name
    bl save_ok
    cbz r0, 2f
    add r0, r7, #S_NAME
    str r0, [r7, #S_SAVENAME]
    mov r1, r0
    mov r0, r4
    mov r2, r6
    call SAVEAS
    movs r0, #0
    str r0, [r7, #S_SAVENAME]
    ldrb r0, [r7, #S_SAVEFAIL]
    cbz r0, 3f
    b 4f
2:
    mov r0, r4                              @ refused: nothing written; stock's done event
    movs r1, #EVT_DONE
    movs r2, #0
    bl post_evt_val
4:
    mov r0, r4
    bl save_failed
3:
    pop {r3, r4, r5, r6, r7, pc}

@ Replaces PresetMgr_SaveAs(pm, name, 0) for cmd 0x20 (Save). The path is the loaded preset's
@ folder, as stock; a failed doc_save now shows the error instead of only logging.
.thumb_func
hook_save:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl ensure
    movs r1, #0
    strb r1, [r0, #S_SAVEFAIL]
    mov r0, r4
    mov r1, r5
    mov r2, r6
    call SAVEAS
    bl ensure
    ldrb r1, [r0, #S_SAVEFAIL]
    cbz r1, 1f
    mov r0, r4
    movs r1, #EVT_SAVED
    movs r2, #1
    bl post_evt_val
1:
    pop {r4, r5, r6, pc}

@ Wraps both doc_save calls in PresetMgr_SaveAs (r0 = path, r1-r3 = session). Stock only logs
@ when f_open fails. Returns doc_save's result.
.thumb_func
hook_docsave:
    push {r4, lr}
    call DOC_SAVE
    cbnz r0, 1f
    ldr r1, =STATE
    movs r2, #1
    strb r2, [r1, #S_SAVEFAIL]
1:
    pop {r4, pc}

@ Wraps PresetMgr_CopyFile(pm, src, dst) in Save As. A path of PATH_MAX characters was cut by
@ path_join, so the copy would land under the wrong name: fail it instead. Stock then stops
@ copying and posts EVT_SAVED with an error.
.thumb_func
hook_copy:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r0, r5
    call STRLEN
    cmp r0, #PATH_MAX
    bhs 1f
    mov r0, r6
    call STRLEN
    cmp r0, #PATH_MAX
    bhs 1f
    mov r0, r4
    mov r1, r5
    mov r2, r6
    call COPYFILE
    pop {r4, r5, r6, pc}
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ cmd 0x22 after a Save As or New that wrote no preset.xml. The UI posts RequestLoad(new name)
@ after both; stock's Load would fail and fall back to the first row that loads, replacing the
@ session with another preset. Answer it from memory instead: PresetMgr still holds the session
@ the request copied, and event 3 success hands it back under the old name. r0 = pm.
@ Returns r0 = 1 when the command was answered.
.thumb_func
load_saved:
    push {r4, r5, r6, lr}
    mov r4, r0
    bl ensure
    mov r5, r0
    ldrb r0, [r5, #S_REFUSED]
    cbz r0, 1f
    movs r0, #0
    strb r0, [r5, #S_REFUSED]
    add r0, r4, #0x18
    call STR_CSTR
    mov r1, r0
    add r0, r5, #S_NAME2
    bl copy_name
    add r1, r5, #S_NAME
    call STRCMP
    cbnz r0, 1f
    add r0, r4, #0x394
    call STR_CSTR
    mov r1, r0
    add r0, r4, #0x390
    call STR_ASSIGN
    mov r0, r4
    movs r1, #EVT_LOADED
    movs r2, #1
    bl post_evt_val
    movs r0, #1
    pop {r4, r5, r6, pc}
1:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ Replaces path_join(dest, unused, base, comp) (0x080431A4): dest = base + '\' + comp, cut at
@ PATH_MAX characters like stock. Stock built the result with strncat in a 256-byte stack buffer,
@ which overran it (and the return address) once base + comp passed ~280 characters, e.g. a
@ sample copied by Save As in a deep folder. Any task; stack only.
.thumb_func
hook_pathjoin:
    push {r4, r5, r6, lr}
    sub sp, #0x100
    mov r4, r0
    mov r5, r3
    movs r6, #0
    cbz r2, 2f
1:
    ldrb r3, [r2], #1
    cbz r3, 2f
    strb r3, [sp, r6]
    adds r6, #1
    cmp r6, #PATH_MAX
    blo 1b
2:
    cbz r6, 3f
    add r3, sp, r6
    ldrb r3, [r3, #-1]
    cmp r3, #0x5C
    beq 4f
3:
    cmp r6, #PATH_MAX
    bhs 4f
    movs r3, #0x5C
    strb r3, [sp, r6]
    adds r6, #1
4:
    cbz r5, 6f
    ldrb r3, [r5]
    cmp r3, #0x5C
    it eq
    addeq r5, #1
5:
    cmp r6, #PATH_MAX
    bhs 6f
    ldrb r3, [r5], #1
    cbz r3, 6f
    strb r3, [sp, r6]
    adds r6, #1
    b 5b
6:
    movs r3, #0
    strb r3, [sp, r6]
    mov r0, r4
    mov r1, sp
    adds r2, r6, #1
    call MEMCPY
    mov r0, r4
    add sp, #0x100
    pop {r4, r5, r6, pc}

@ Replaces the preset.als export that follows Save As (r0 = pm, r1 = new name): it belongs
@ next to the new preset.xml in the browse folder. Skipped when Save As wrote no preset.xml.
.thumb_func
hook_saveas_als:
    push {r4, r5, r6, lr}
    mov r5, r0
    mov r6, r1
    bl ensure
    mov r4, r0
    ldrb r0, [r4, #S_REFUSED]
    cbnz r0, 1f
    movs r0, #1
    strb r0, [r4, #S_INLOAD]
    mov r0, r5
    mov r1, r6
    call ALS_EXPORT
    movs r0, #0
    strb r0, [r4, #S_INLOAD]
1:
    pop {r4, r5, r6, pc}

@ Replaces the Delete handler call (cmd 0x17). r0 = pm, r1 = row name.
.thumb_func
hook_delete:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r0, r1
    movs r1, #1
    bl row_begin
    cbz r0, 1f
    mov r5, r0
    bl cache_inval
    mov r1, r5
    mov r0, r4
    call DELETE
    b 2f
1:
    mov r0, r4
    movs r1, #EVT_DONE
    movs r2, #0
    bl post_evt
2:
    bl row_end
    pop {r4, r5, r6, pc}

@ Replaces the Clean handler call (cmd 0x24). r0 = pm, r1 = command (name string at +8).
@ The stock handler posts no event, so a rejected Clean posts none either.
.thumb_func
hook_clean:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    add r0, r5, #8
    call STR_CSTR
    movs r1, #1
    bl row_begin
    cbz r0, 1f
    mov r0, r4
    mov r1, r5
    call CLEAN
1:
    bl row_end
    pop {r4, r5, r6, pc}

@ Replaces the New-preset handler call (cmd 0x15). r0 = pm, r1 = name.
.thumb_func
hook_new:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r0, r1
    movs r1, #0
    bl row_begin
    cbz r0, 1f
    mov r5, r0
    bl ensure
    movs r1, #0
    strb r1, [r0, #S_REFUSED]
    strb r1, [r0, #S_SAVEFAIL]
    mov r0, r5
    bl save_ok
    cbz r0, 3f
    bl cache_inval
    mov r1, r5
    mov r0, r4
    call NEWPRESET
    bl ensure
    ldrb r0, [r0, #S_SAVEFAIL]
    cbz r0, 2f
    mov r0, r4
    bl save_failed
    b 2f
3:
    mov r0, r4
    bl save_failed
1:
    mov r0, r4
    movs r1, #EVT_DONE
    movs r2, #0
    bl post_evt
2:
    bl row_end
    pop {r4, r5, r6, pc}

@ Replaces the Rename handler call (cmd 0x14). r0 = pm, r1 = old row name, r2 = new name.
@ Folders may be renamed; ".." may not. Renaming a marked row rebuilds the list so the marker
@ follows the new name.
.thumb_func
hook_rename:
    push {r3, r4, r5, r6, r7, lr}
    mov r4, r0
    mov r6, r2
    ldrb r7, [r1]
    mov r0, r1
    movs r1, #0
    bl row_begin
    cbz r0, 1f
    mov r5, r0
    bl ensure
    add r0, r0, #S_NAME2
    mov r1, r6
    bl copy_name
    mov r6, r0
    ldrb r1, [r0]
    cbz r1, 1f
    ldr r1, =s_dotdot
    call STRCMP
    cbz r0, 1f
    bl cache_inval
    mov r0, r4
    mov r1, r5
    mov r2, r6
    call RENAME
    bl row_end
    cmp r7, #0x2F
    bne 2f
    add r0, r4, #0x394
    call STR_CSTR
    mov r1, r0
    mov r0, r4
    call BUILDLIST
    b 2f
1:
    bl row_end
    mov r0, r4
    movs r1, #EVT_DONE
    movs r2, #0
    bl post_evt
2:
    pop {r3, r4, r5, r6, r7, pc}

@ ---- UI task (RAM only)

@ Posts CMD_CHECK for a row, the way PresetMgr_Request* post commands. r0 = name, r1 = row index.
.thumb_func
post_check:
    push {r3, r4, r5, r6, r7, lr}
    mov r4, r0
    mov r5, r1
    ldr r6, =PM
    movs r3, #0
    strb r3, [r6, #4]
    str r5, [r6]
    strb r3, [r6, #5]
    add r0, r6, #8
    call STR_CLEAR
    add r0, r6, #0xC
    call STR_CLEAR
    add r0, r6, #8
    mov r1, r4
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
    pop {r3, r4, r5, r6, r7, pc}

@ Replaces SessionMgr_LoadBank(app, idx, 1) in the Load-button case of the app event
@ handler; the following screen switch is skipped by a patched branch. Posts CMD_CHECK
@ with the row name so pcmStreamer can decide between load and navigate.
.thumb_func
hook_ui_load:
    push {r4, r5, r6, lr}
    sub sp, #0x100
    mov r4, r0
    mov r5, r1
    bl ensure
    str r4, [r0, #S_APP]
    ldr r0, =PM
    add r0, r0, #0x32C
    mov r1, r5
    mov r2, sp
    call LIST_GET
    cbz r0, 1f
    mov r0, sp
    mov r1, r5
    bl post_check
    b 2f
1:
    mov r0, r4
    mov r1, r5
    movs r2, #1
    call LOADBANK
    mov r0, r4
    movs r1, #2
    movs r2, #0
    movs r3, #0
    call SCREEN
2:
    add sp, #0x100
    pop {r4, r5, r6, pc}

@ Posts the same cmd 0x30 as Load on "..". Uses the displayed list (row 0 is
@ "..") so it still works if S_BROWSE was reset. Returns 0 if it posted.
.thumb_func
folder_back:
.ifdef MIX_EXT
@ 3.1.Q: on the preset screen (7) below the top level, BACK / ^ go up. The button scan and
@ App_HandleInput type 7 both call this for one press, and the browse path only changes once
@ pcmStreamer has run the check, so a second call within 300 ms is ignored.
    push {r4, lr}
    ldr r0, =0x24020088 + 0x8CA4            @ App screen byte
    ldrb r0, [r0]
    cmp r0, #7
    bne 1f
    bl ensure
    add r0, r0, #S_BROWSE
    bl is_top
    cbnz r0, 1f
    ldr r0, =0x24015CE4                     @ uwTick
    ldr r0, [r0]
    ldr r1, =0x38005180                     @ SRAM4: tick of the last up
    ldr r2, [r1]
    subs r2, r0, r2
    cmp r2, #300
    blo 2f
    str r0, [r1]
    ldr r0, =s_dotdot
    movs r1, #0
    bl post_check
2:
    movs r0, #0
    pop {r4, pc}
1:
    movs r0, #1
    pop {r4, pc}
.else
    push {r4, lr}
    sub sp, #0x100
    ldr r0, =PM
    add r0, r0, #0x32C
    movs r1, #0
    mov r2, sp
    call LIST_GET
    cbz r0, 1f
    mov r0, sp
    mov r1, sp
    bl copy_name
    ldr r1, =s_dotdot
    call STRCMP
    cbnz r0, 1f
    ldr r0, =s_dotdot
    movs r1, #0
    bl post_check
    movs r0, #0
    add sp, #0x100
    pop {r4, pc}
1:
    movs r0, #1
    add sp, #0x100
    pop {r4, pc}
.endif

@ Replaces ui_post_event_up for type-2 key 0x7f. Uses RequestList, not cmd 0x30: BACK
@ runs in the key handler, and a custom command was easy to miss on the device.
.thumb_func
hook_back:
    push {r0, r1, r4, lr}
    bl folder_back
    cmp r0, #0
    pop {r0, r1, r4, lr}
    bne 1f
    bx lr
1:
    ldr r12, =UI_POST_UP
    bx r12

@ Replaces add.w r5, r0, #0x8000 at the start of App_HandleInput type 7.
@ Type 7 is hardware BACK (PC2) on every screen. Nested: post .. and leave
@ HandleInput (do not call from the bench). At the top: stock type-7 path.
.thumb_func
hook_type7:
    push {r0, r1, r2, r3, r4, lr}
.ifdef WARP_SONG_DJ
    bl dj_back_key              @ 3.1.T Song DJ (dj.c): r0 = app; 0 = BACK taken (audio loop capture)
    cmp r0, #0
    beq 2f
.endif
    bl folder_back
    cmp r0, #0
2:
    pop {r0, r1, r2, r3, r4, lr}
    bne 1f
    ldr r12, =INPUT_EPI
    bx r12
1:
    add.w r5, r0, #0x8000
    bx lr

@ Replaces HAL_GPIO_ReadPin in the 13-button scan. On a PC2 falling edge
@ (hardware BACK), post .. if the list starts with it. Stock still enqueues.
.thumb_func
hook_gpio:
    push {r1, r2, r3, r4, lr}
    ldr r12, =GPIO_READ
    blx r12
    ldr r1, [r4]
    ldr r2, =GPIOC
    cmp r1, r2
    bne 1f
    ldrh r1, [r4, #4]
    cmp r1, #4
    bne 1f
    cbnz r0, 1f
    ldrb r1, [r4, #0xe]
    cbz r1, 1f
    push {r0}
    bl folder_back
    pop {r0}
1:
    pop {r1, r2, r3, r4, lr}
    bx lr

@ Replaces the unhandled-event return in preset_screen_key (movs r0,#1; b epilogue).
@ Entered with b.w, not bl: a bl would leave an extra return address.
@ r3 = event type, r4 = widget.
.thumb_func
hook_back_evt:
    cmp r3, #0x7f
    beq 1f
    movs r0, #1
    ldr r12, =KEY_EXIT
    bx r12
1:
    push {r1, r4, lr}
    bl folder_back
    pop {r1, r4, lr}
    cbnz r0, 2f
    movs r0, #0
    ldr r12, =KEY_EXIT
    bx r12
2:
    mov r0, r4
    push {lr}
    ldr r12, =UI_POST_UP
    blx r12
    pop {lr}
    movs r0, #0
    ldr r12, =KEY_EXIT
    bx r12

@ Replaces keyboard_set_text(kb, row) where Save As pre-fills the keyboard with the selected row
@ (preset_screen_key case 0x21). A group row ("/name") would save into that group folder; pre-fill
@ the loaded preset's name instead. UI task, RAM only.
.thumb_func
hook_kb_saveas:
    ldrb r2, [r1]
    cmp r2, #0x2F
    bne 1f
    push {r0, r1, r4, lr}
    ldr r0, =APP
    call CUR_NAME
    pop {r0, r1, r4, lr}
1:
    ldr r12, =KB_SET
    bx r12

@ Replaces list_get(display list, i, buf) in unique_name (0x08091244), which numbers a typed
@ Save As / New name that clashes with a row ("Sub" -> "Sub 1"). Rows come back without the
@ group marker, so a name that matches a group folder is numbered too. UI task, RAM only.
.thumb_func
hook_uname_get:
    push {r2, lr}
    call LIST_GET
    pop {r2, lr}
    cbz r0, 2f
    ldrb r1, [r2]
    cmp r1, #0x2F
    bne 2f
1:
    ldrb r1, [r2, #1]
    strb r1, [r2], #1
    cmp r1, #0
    bne 1b
2:
    bx lr

@ ---- command dispatcher (pcmStreamer)

@ Replaces ldrb r3, [r4, #0x14]; subs r3, #4 (r4 = PresetMgr). CMD_CHECK is handled here
@ and returns r3 above the jump-table range so the dispatcher returns.
.thumb_func
hook_disp:
    ldrb r3, [r4, #0x14]
    cmp r3, #CMD_CHECK
    beq 1f
    cmp r3, #CMD_LOAD
    beq 2f
3:
    subs r3, #4
    bx lr
2:
    push {r0, r1, r2, lr}
    mov r0, r4
    bl load_saved
    mov r3, r0
    pop {r0, r1, r2, lr}
    cbz r3, 4f
    movs r3, #(CMD_CHECK - 4)
    bx lr
4:
    ldrb r3, [r4, #0x14]
    b 3b
1:
    push {r3, lr}
    mov r0, r4
    bl fm_check
    pop {r3, lr}
    movs r3, #(CMD_CHECK - 4)
    bx lr

@ CMD_CHECK: r0 = pm; row name in the command string at +0x18, row index at +0x10.
@   ".."                          -> browse up, rebuild the list
@   <browse>\name\preset.xml      -> post EVT_LOAD (UI runs the stock load)
@   <browse>\name is a directory  -> browse into it, rebuild the list
@   anything else                 -> post EVT_LOAD (stock load handles it, e.g. root xml)
@ A leading '/' (folder marker) is ignored.
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
    bl copy_name

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
    bl push_comp                            @ too deep: stay (a load would fall back to row 0)
4:
    add r0, r4, #0x394
    call STR_CSTR
    mov r1, r0
    mov r0, r4
    call BUILDLIST
    b 6f
5:
    mov r0, r4
    movs r1, #EVT_LOAD
    ldr r2, [r4, #0x10]
    bl post_evt
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
s_presetxml:
    .asciz "preset.xml"

.align 2
.ltorg

.include "repitch.S"
.ifndef WARP_RUBBER                     @ 2048 slices grow into SRAM1+2, which a bank-2 leaf owns
.ifndef WARP_PLAIN                      @ and 3.1.Q never had them
.include "slice.S"
.endif
.endif
.ifdef WARP_DECK                        @ Clip Deck in the combined 3.1.T build (-I ../3.1.9-clip-deck)
.include "deck.S"
.endif

.align 2
.ltorg
