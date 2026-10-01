// 0x08092f5c preset_cmd_dispatch  (Ghidra decompile, 3.1.9)

/* pops one command from +0x20 ring (read +0x120, write +0x124) */

uint preset_cmd_dispatch(uint param_1)

{
  bool bVar1;
  char *pcVar2;
  char *pcVar3;
  byte *pbVar4;
  int iVar5;
  undefined4 *puVar6;
  uint uVar7;
  int iVar8;
  
  uVar7 = param_1;
  if (*(int *)(param_1 + 0x124) != *(int *)(param_1 + 0x120)) {
    puVar6 = (undefined4 *)(param_1 + 0x10);
    uVar7 = *(uint *)(param_1 + 0x120) & 0xf;
    iVar5 = uVar7 * 0x10;
    iVar8 = param_1 + uVar7 * 0x10;
    *(undefined4 *)(param_1 + 0x10) = *(undefined4 *)(iVar8 + 0x20);
    *(undefined1 *)(param_1 + 0x14) = *(undefined1 *)(iVar8 + 0x24);
    *(undefined1 *)(param_1 + 0x15) = *(undefined1 *)(iVar8 + 0x25);
    str_copy((int *)(param_1 + 0x18),(int *)(iVar5 + 8 + param_1 + 0x20));
    str_copy((int *)(param_1 + 0x1c),(int *)(iVar5 + 0xc + param_1 + 0x20));
    uVar7 = ring_advance((int *)(param_1 + 0x120));
    switch(*(undefined1 *)(param_1 + 0x14)) {
    case 4:
      pbVar4 = (byte *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = PresetMgr_LoadSettings(param_1,pbVar4);
      break;
    case 5:
      pbVar4 = (byte *)str_cstr((int *)(param_1 + 0x18));
      bVar1 = PresetMgr_SaveSettings(param_1,pbVar4);
      uVar7 = (uint)bVar1;
      break;
    case 0x10:
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = PresetMgr_BuildList(param_1,pcVar3);
      break;
    case 0x12:
      uVar7 = FUN_080913d4(param_1,(int)puVar6);
      break;
    case 0x14:
      pbVar4 = (byte *)str_cstr((int *)(param_1 + 0x18));
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x1c));
      uVar7 = FUN_08092bb8(param_1,pbVar4,pcVar3);
      break;
    case 0x15:
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = FUN_08092c58(param_1,pcVar3);
      break;
    case 0x16:
      uVar7 = FUN_08092eec(param_1,(int)puVar6);
      break;
    case 0x17:
      pbVar4 = (byte *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = FUN_08092e68(param_1,pbVar4);
      break;
    case 0x19:
      uVar7 = FUN_08091ab4(param_1,puVar6);
      break;
    case 0x20:
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = PresetMgr_SaveAs(param_1,pcVar3,(char *)0x0);
      if (*(char *)(param_1 + 0x15) != '\0') {
        pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
        bVar1 = FUN_08091888(param_1,pcVar3);
        uVar7 = (uint)bVar1;
      }
      break;
    case 0x21:
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
      pcVar2 = (char *)str_cstr((int *)(param_1 + 0x1c));
      uVar7 = PresetMgr_SaveAs(param_1,pcVar3,pcVar2);
      if (*(char *)(param_1 + 0x15) != '\0') {
        pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
        bVar1 = FUN_08091888(param_1,pcVar3);
        uVar7 = (uint)bVar1;
      }
      break;
    case 0x22:
      pcVar3 = (char *)str_cstr((int *)(param_1 + 0x18));
      uVar7 = PresetMgr_Load(param_1,pcVar3);
      break;
    case 0x23:
      uVar7 = PresetMgr_PackPreset(param_1,(int)puVar6);
      break;
    case 0x24:
      uVar7 = PresetMgr_CleanDelete(param_1,(int)puVar6);
    }
  }
  return uVar7;
}


