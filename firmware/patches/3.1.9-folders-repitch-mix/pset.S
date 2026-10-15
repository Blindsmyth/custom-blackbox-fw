@ Preset list opens on its first row (the first folder) instead of the loaded preset.
@ docs/firmware/README.md "Presets".
@
@ Preset screen onEvent 0x080BDE60: event 0x68 selects the loaded preset by name, and
@ event 0x149 (list rebuilt) selects the pending row name (+0x1CDF, kept around a load) or
@ else the loaded preset, falling back to row 0 when the name isn't listed. Both look the
@ name up with FUN_08097AC8(sessionmgr, buf) -> 0 when there is none. With preset folders
@ the browse path starts in the loaded preset's folder, so the list opened on that preset.
@ Both calls now report "no name": 0x68 leaves the selection alone and 0x149 picks row 0.
@ The loaded preset's own highlight (preset_screen_draw) keeps calling FUN_08097AC8.

.global pset_no_current

@ bl sites 0x080BDE96 / 0x080BDFEE (was bl FUN_08097AC8).
.thumb_func
pset_no_current:
    movs    r0, #0
    bx      lr
