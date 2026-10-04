@ 3.1.V: group folders in the 3.1.9 preset browser.
@
@ State lives in SRAM4 at 0x38000000 (reset by fm_boot each boot). Not DTCM: stock's small-block
@ allocator (0x080443A8) hands out DTCM from 0x20000000 upward. SRAM4 has no stock references and
@ needs no clock enable.
@   +0x000 magic, +0x004 in_load, +0x008 app pointer, +0x00C Save As dest name,
@   +0x100 browse path, +0x200 base path, +0x300 / +0x400 name buffers,
@   +0x500 FILINFO + path scratch (so BuildList does not grow the pcmStreamer stack),
@   +0x720 cached browse path / valid flag / group-name list.
@ Both paths start as "Presets", which is the stock behaviour.
@
@ App_Update rebuilds the preset list on a timer even on the pads screen. 3.1.O called f_stat
@ twice per row on every rebuild (4,227 sector reads for 146 rows) on pcmStreamer's 8 KB stack.
@ 3.1.P stats each folder once per browse path, then reuses the cached "/name" marks.
@
@ Task rules (docs/map-319.md): FatFs is only called from pcmStreamer (command dispatcher and
@ BuildList). UI-task hooks only touch RAM and post commands. Card writes only come from the stock
@ handlers (Save As, Delete, Rename, New, Clean); the hooks only choose the folder they act on.

.syntax unified
.thumb
.cpu cortex-m7

.equ STATE,       0x38000000
.equ MAGIC,       0x4D464231
.equ S_INLOAD,    0x004
.equ S_APP,       0x008
.equ S_SAVENAME,  0x00C
.equ S_BROWSE,    0x100
.equ S_BASE,      0x200
.equ S_NAME,      0x300
.equ S_NAME2,     0x400
.equ S_FILINFO,   0x500
.equ S_PATHBUF,   0x620
.equ S_CPATH,     0x720
.equ S_CVALID,    0x820
.equ S_GCNT,      0x824
.equ S_GNAMES,    0x828
.equ GNLEN,       64
.equ GMAX,        128
.equ S_PVALID,    0x2828
.equ S_PGCNT,     0x282C
.equ S_PPATH,     0x2830
.equ S_PGNAMES,   0x2930
.equ S_SIZE,      0x4930

.equ PM,          0x2401F508
.equ SD,          0x240021A4
.equ ENGINE,      0x2400A9C0
.equ PRESETS,     0x080CEBC4
.equ XML_EXT,     0x080CEBC0
.equ CMD_CHECK,   0x30
.equ EVT_LOAD,    0x30
.equ EVT_DONE,    0x27

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

@ Drops the last component of the browse path, never going above "Presets".
@ r0 = path. Going up a browse level restores the parent folder's / marks.
.thumb_func
pop_comp:
    push {r4, r5, lr}
    mov r4, r0
    ldr r0, =STATE
    add r0, #S_BROWSE
    cmp r4, r0
    ite eq
    moveq r5, #1
    movne r5, #0
    mov r0, r4
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
    cbz r5, 4f
    bl cache_restore
4:
    pop {r4, r5, pc}

@ Appends "\name" to the path if it fits. r0 = path, r1 = name. Returns r0 = 1 on success.
.thumb_func
push_comp:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    ldr r0, =STATE
    add r0, #S_BROWSE
    cmp r4, r0
    bne 0f
    bl cache_save
    bl cache_clear
0:
    mov r0, r4
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

@ Drops the current group-name cache. Clobbers r0-r3, r12.
.thumb_func
cache_clear:
    push {lr}
    bl ensure
    movs r1, #0
    strb r1, [r0, #S_CVALID]
    str r1, [r0, #S_GCNT]
    pop {pc}

@ Current plus parent slot (rename / delete / new). Clobbers r0-r3, r12.
.thumb_func
cache_inval:
    push {r4, lr}
    bl cache_clear
    bl ensure
    movs r1, #0
    ldr r2, =S_PVALID
    strb r1, [r0, r2]
    pop {r4, pc}

@ Copy current marks into the parent slot before entering a folder.
.thumb_func
cache_save:
    push {r4, lr}
    bl ensure
    mov r4, r0
    movs r1, #1
    ldr r2, =S_PVALID
    strb r1, [r4, r2]
    ldr r1, [r4, #S_GCNT]
    ldr r2, =S_PGCNT
    str r1, [r4, r2]
    ldr r2, =S_PPATH
    add r0, r4, r2
    add r1, r4, #S_BROWSE
    movs r2, #0xff
    call STRNCPY
    ldr r2, =S_PGNAMES
    add r0, r4, r2
    add r1, r4, #S_GNAMES
    mov.w r2, #0x2000
    call MEMCPY
    pop {r4, pc}

@ After pop_comp the browse path is the parent. Reuse its marks if we saved them.
.thumb_func
cache_restore:
    push {r4, lr}
    bl ensure
    mov r4, r0
    ldr r2, =S_PVALID
    ldrb r1, [r4, r2]
    cbz r1, 1f
    ldr r2, =S_PPATH
    add r0, r4, r2
    add r1, r4, #S_BROWSE
    call STRCMP
    cbnz r0, 1f
    ldr r2, =S_PGCNT
    ldr r1, [r4, r2]
    str r1, [r4, #S_GCNT]
    movs r1, #1
    strb r1, [r4, #S_CVALID]
    add r0, r4, #S_GNAMES
    ldr r2, =S_PGNAMES
    add r1, r4, r2
    mov.w r2, #0x2000
    call MEMCPY
    add r0, r4, #S_CPATH
    add r1, r4, #S_BROWSE
    movs r2, #0xff
    call STRNCPY
    pop {r4, pc}
1:
    bl cache_clear
    pop {r4, pc}

@ r0 = name. Returns r0 = 1 when the name is in the group cache.
.thumb_func
cache_has:
    push {r4, r5, r6, lr}
    mov r5, r0
    bl ensure
    ldr r6, [r0, #S_GCNT]
    add r4, r0, #S_GNAMES
1:
    cbz r6, 2f
    mov r0, r4
    mov r1, r5
    call STRCMP
    cbz r0, 3f
    add r4, #GNLEN
    subs r6, #1
    b 1b
3:
    movs r0, #1
    pop {r4, r5, r6, pc}
2:
    movs r0, #0
    pop {r4, r5, r6, pc}

@ r0 = name. Stores it in the group cache when there is room.
.thumb_func
cache_add:
    push {r4, r5, r6, lr}
    mov r5, r0
    bl cache_has
    cbnz r0, 1f
    bl ensure
    ldr r6, [r0, #S_GCNT]
    cmp r6, #GMAX
    bhs 1f
    adds r1, r6, #1
    str r1, [r0, #S_GCNT]
    add r0, r0, #S_GNAMES
    add r0, r0, r6, lsl #6
    mov r1, r5
    movs r2, #(GNLEN - 1)
    call STRNCPY
    movs r1, #0
    strb r1, [r0, #(GNLEN - 1)]
1:
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

@ r0 = name (no marker). Returns r0 = 1 when <browse>\name is a directory without preset.xml.
@ Uses SRAM4 scratch, not the stack. When the cache matches the browse path this is RAM only.
.thumb_func
is_group:
    push {r4, r5, r6, lr}
    mov r5, r0
    bl ensure
    mov r4, r0
    ldrb r1, [r4, #S_CVALID]
    cbz r1, 3f
    add r0, r4, #S_CPATH
    add r1, r4, #S_BROWSE
    call STRCMP
    cbnz r0, 3f
    mov r0, r5
    pop {r4, r5, r6, lr}
    b cache_has
3:
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
    b 2f
1:
    movs r0, #0
2:
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

@ Replaces list_add(display list, name) for each directory row: group folders get a leading '/'.
@ r0 = list, r1 = name.
.thumb_func
hook_mark:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r0, r5
    bl is_group
    cbz r0, 1f
    bl ensure
    ldrb r1, [r0, #S_CVALID]
    cbnz r1, 2f
    mov r0, r5
    bl cache_add
2:
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
    bl ensure
    add r1, r0, #S_BROWSE
    add r0, r0, #S_CPATH
    movs r2, #0xff
    call STRNCPY
    ldr r0, =STATE
    movs r1, #1
    strb r1, [r0, #S_CVALID]
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

@ Replaces PresetMgr_SaveAs(pm, new, old) for cmd 0x21. The new name goes through a private
@ buffer so hook_join can tell the destination (browse folder) from the sample sources (loaded
@ preset's folder) even when both names are equal. Plain Save (cmd 0x20) is not hooked.
.thumb_func
hook_saveas:
    push {r3, r4, r5, r6, r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl ensure
    mov r7, r0
    add r0, r7, #S_NAME
    mov r1, r5
    bl copy_name
    str r0, [r7, #S_SAVENAME]
    mov r1, r0
    mov r0, r4
    mov r2, r6
    call SAVEAS
    movs r0, #0
    str r0, [r7, #S_SAVENAME]
    pop {r3, r4, r5, r6, r7, pc}

@ Replaces the preset.als export that follows Save As (r0 = pm, r1 = new name): it belongs
@ next to the new preset.xml in the browse folder.
.thumb_func
hook_saveas_als:
    push {r4, r5, r6, lr}
    mov r5, r0
    mov r6, r1
    bl ensure
    mov r4, r0
    movs r0, #1
    strb r0, [r4, #S_INLOAD]
    mov r0, r5
    mov r1, r6
    call ALS_EXPORT
    movs r0, #0
    strb r0, [r4, #S_INLOAD]
    pop {r4, r5, r6, pc}

@ Replaces the Delete handler call (cmd 0x17). r0 = pm, r1 = row name.
.thumb_func
hook_delete:
    push {r4, lr}
    mov r4, r0
    mov r0, r1
    movs r1, #1
    bl row_begin
    cbz r0, 1f
    mov r1, r0
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
    pop {r4, pc}

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
    push {r4, lr}
    mov r4, r0
    mov r0, r1
    movs r1, #0
    bl row_begin
    cbz r0, 1f
    mov r1, r0
    mov r0, r4
    call NEWPRESET
    b 2f
1:
    mov r0, r4
    movs r1, #EVT_DONE
    movs r2, #0
    bl post_evt
2:
    bl row_end
    pop {r4, pc}

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
    mov r0, r4
    mov r1, r5
    mov r2, r6
    call RENAME
    bl row_end
    cmp r7, #0x2F
    bne 2f
    bl cache_inval
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
    bl folder_back
    cmp r0, #0
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
