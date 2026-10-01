# Firmware versions compared

Nine builds were analyzed headlessly with the same pipeline as 3.1.9 (`tools/ghidra_import.sh <ver> import nodecomp`): 1.0.2, 1.3.6, 1.5.1, 1.7.F, 2.0.E, 2.1.5, 3.0.9, 3.1.2 and 3.1.9. The remaining builds in `firmware/bins/` can be added the same way. Names were carried from 3.1.9 with `tools/carry_names.py`, which uses two matchers:

- **String anchors.** A string used by exactly one function on both sides.
- **Shape hashes.** A unique hash of the function's instruction sequence, with immediates and addresses removed. This matches library code and unchanged application code.

Results go to `docs/symbols/<ver>.auto.csv`, and `tools/ghidra_import.sh` applies them when no hand-made `<ver>.csv` exists.

## What stays the same

- Every build is linked at `0x08040000`, with the reset vector at the start of the file and the same installer.
- The same four threads exist in every build from 1.0.2 to 3.1.9: `defaultTask`, `audioTask`, `pcmStreamer`, `USBH_Thread`. The UI/streamer split is original to the design, not a late change.
- The same BoomboxFramework code base throughout (`SDMgr.cpp`, the `log_printf` call pattern, the `str_*` string class, `ring_advance`), plus FatFs and newlib.

## What changes

| Version | Functions | Strings | Initial SP | PresetMgr | Notes |
| --- | --- | --- | --- | --- | --- |
| 1.0.2 | 2163 | 704 | `0x24074148` (AXI) | no | Presets are root `*.xml` files |
| 1.3.6 | 2288 | 767 | `0x2407EA40` | no | |
| 1.5.1 | 2363 | 826 | `0x2407C900` | **yes (7 `PresetMgr::` strings)** | `\Presets\<name>\` folders, Pack, Clean (changelog) |
| 1.7.F | 2361 | 1158 | `0x24059F18` | yes | |
| 2.0.E | 2473 | 1074 | `0x24050B80` | yes | |
| 2.1.5 | 2481 | 1081 | `0x24052158` | yes | |
| 3.0.9 | 2519 | 1110 | `0x24053F80` | yes | |
| 3.1.2 | 2561 | 1117 | `0x240609B0` | yes | |
| 3.1.9 | 2622 | 1205 | `0x20020000` (DTCM) | yes | Stack moved to DTCM; image relinked (FatFs `~0x08057000` → `0x08084000`, PresetMgr `~0x0808D000` → `0x08091000`) |

Before 3.1.9, the initial SP was the end of RAM use in AXI SRAM. 3.1.9 is the first build whose main stack is in DTCM. It still leaves the rest of DTCM unused.

## Preset handling across versions (changelog + code)

From `firmware/manifest.json` notes:

- **1.2.2**: file and preset names can be edited from the start.
- **1.4.0**: the load screen lists directories first; names containing a period broke the file system (fixed in 1.4.0/1.4.3).
- **1.5.1**: presets move into subfolders of `\Presets`. Recordings go to the current preset folder. New Pack and Clean commands. `PresetMgr` first appears in the code here. The legacy-migration branch of `PresetMgr_TryLoad` (root `name.xml` → `Presets\name\preset.xml`) dates from this change.
- **1.6.5**: directories can be deleted.
- **2.9.1 / 3.0.1**: preset format only partly compatible with older presets.

No release ever supported nested preset folders, so there is no stock code to borrow for that. What every version since 1.5.1 does is the same flat scheme: `PresetMgr_BuildList` lists `Presets\*.`, and `PresetMgr_TryLoad` opens `Presets\<name>\preset.xml`.

## Key functions by version

Addresses carried from 3.1.9 names. "-" means no confident match (the function exists but changed, or has no unique anchor).

| Function | 1.0.2 | 1.3.6 | 1.5.1 | 1.7.F | 2.0.E | 2.1.5 | 3.0.9 | 3.1.2 | 3.1.9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `main` | 08041270 | 080410C4 | 080410C4 | 08041210 | 08041058 | 08041058 | 08041058 | 08041184 | 08044188 |
| `defaultTask` | 08040EE8 | 08040CF0 | 08040CF0 | 08040CE4 | 08040CE4 | 08040CE4 | 08040CE4 | 08040E10 | 08043DEC |
| `log_printf` | 08043C00 | 08043A78 | 08043778 | 08043B94 | 080438C4 | 0804393C | 08043A70 | 08043BC4 | 08043DE4 |
| `SessionMgr_LoadBank` | 080700DC | 0807620C | 08077048 | 080790C4 | 08079D34 | 0807A9C8 | 0807D678 | 0807DF00 | 0809E218 |
| `SessionMgr_FinalizeBankLoading` | 08072ED4 | 08073D10 | 08074B18 | 08077CF4 | 08078950 | 08079570 | 0807C00C | 0807C7F4 | 0809CB4C |
| `PresetMgr_RequestLoad` | - | - | 0807EAE8 | 080860AC | 08085E50 | 08087014 | 0808B118 | 0808B9DC | 08090B78 |
| `PresetMgr_SaveAs` | - | - | 0808001C | 08087748 | 080875AC | 080887B8 | 0808C894 | 0808D150 | 08091E80 |
| `PresetMgr_CopyFile` | - | - | 0807FF20 | 08087648 | 080874AC | 080886B8 | 0808C794 | 0808D050 | 08091D80 |
| `PresetMgr_PackPreset` | - | - | 0808049C | 08087BE4 | 08087A48 | 08088C54 | 0808CD3C | 0808D5F8 | 080922E4 |
| `PresetMgr_CleanDelete` | - | - | - | 08088104 | 08087F68 | 08089174 | 0808D264 | 0808DB28 | 0809283C |
| `preset_xml_path` | - | - | 0807FA80 | 08087160 | - | - | 0808C214 | 0808CAD0 | 08091860 |
| `SDMgr_CheckMount` | - | - | - | - | 080449A8 | 08044A20 | 08044CAC | 08044E50 | 08042C90 |
| `BitmapFile_SaveToFile` | 08063F84 | 08060164 | 08060AC8 | 08063A88 | 08063DA0 | 08063FF8 | 080646D8 | 08064AC4 | 0808BF90 |
| `f_findfirst` | - | - | - | 08056FD8 | 08056F00 | 08056F78 | 08057200 | 08057542 | 0808722E |
| `f_close` | - | - | - | 08056AB8 | 080569E0 | 08056A58 | 08056CE0 | 080570C6 | 08086D76 |
| `memcpy` | 08094CFC | 0809BDB8 | 080A1B4C | 080B28FE | 080BC7F6 | 080BDF1E | 080C1F2E | 080C2E9A | 080C8D0C |
| `snprintf` | 08094A38 | 0809BAF4 | 080A1888 | 080B2650 | 080BC548 | 080BDC70 | 080C1C80 | 080C2B68 | 080C89F4 |

Takeaways for patching:

1. Patch addresses are valid for 3.1.9 only. 3.1.9 was relinked, so nothing from a 3.1.2 or older analysis carries over by address.
2. The threads, the PresetMgr command/event rings and the "file work runs in `pcmStreamer`" rule have been stable since 1.5.1. That design is safe to rely on.
3. If the patch is ever ported to another version, rebuild the anchor list with `carry_names.py` first, then re-verify each hook site.
