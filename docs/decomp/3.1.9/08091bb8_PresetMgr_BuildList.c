// 0x08091bb8 PresetMgr_BuildList  (Ghidra decompile, 3.1.9)

/* cmd 0x10: root *.xml + Presets\*. dirs -> +0x32C; <preset>\*.wav -> +0x2EC */

void PresetMgr_BuildList(int param_1,char *param_2)

{
  undefined4 *puVar1;
  undefined4 *puVar2;
  byte *pbVar3;
  bool bVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  int iVar8;
  uint *puVar9;
  uint uVar10;
  int *piVar11;
  uint *puVar12;
  undefined4 local_228;
  char local_128 [255];
  undefined1 local_29;
  
  puVar1 = DAT_08091d64;
  SDMgr_CheckMount(DAT_08091d64);
  piVar11 = (int *)(param_1 + 0x32c);
  list_clear(piVar11);
  puVar9 = (uint *)(param_1 + 0x304);
  list_clear(puVar9);
  puVar12 = (uint *)(param_1 + 0x318);
  list_clear(puVar12);
  SDMgr_ScanNames((int)puVar1,DAT_08091d6c,DAT_08091d68,0,puVar9);
  puVar1 = DAT_08091d64;
  uVar7 = 0;
  local_128[0] = '\0';
  while (bVar4 = list_get(puVar9,uVar7,local_128), pbVar3 = DAT_08091d70, puVar2 = DAT_08091d64,
        bVar4) {
    uVar7 = uVar7 + 1;
    path_strip_ext((char *)&local_228,puVar1,(int)local_128);
    list_add(piVar11,(int)&local_228);
  }
  SDMgr_Mkdir(DAT_08091d64,DAT_08091d70);
  SDMgr_ScanNames((int)puVar2,pbVar3,DAT_08091d74,1,puVar12);
  puVar9 = DAT_08091d7c;
  uVar7 = 0;
  while( true ) {
    bVar4 = list_get(puVar12,uVar7,local_128);
    if (!bVar4) break;
    uVar7 = uVar7 + 1;
    strncpy((char *)&local_228,(int)local_128,1);
    local_228._1_1_ = 0;
    iVar5 = strcmp(&local_228,puVar9);
    if (iVar5 == 0) {
      iVar5 = strnlen(local_128,0x100);
      uVar10 = iVar5 - 1;
      local_228._0_1_ = 0;
      if (0 < (int)uVar10) {
        iVar5 = strnlen(local_128,0x100);
        uVar6 = iVar5 - uVar10 & ~((int)(iVar5 - uVar10) >> 0x1f);
        strncpy((char *)&local_228,(int)(local_128 + uVar6),uVar10);
        *(undefined1 *)((int)&local_228 + (iVar5 - uVar6)) = 0;
      }
      strncpy(local_128,(int)&local_228,0xff);
      local_29 = 0;
      list_add(piVar11,(int)local_128);
    }
  }
  iVar8 = param_1 + 0x128;
  uVar7 = *(uint *)(param_1 + 0x2ac) & 0xf;
  iVar5 = iVar8 + uVar7 * 0x18;
  *(undefined1 *)(iVar8 + uVar7 * 0x18) = 0x27;
  *(undefined4 *)(iVar5 + 8) = 0;
  *(undefined4 *)(iVar5 + 0xc) = 0;
  *(undefined4 *)(iVar5 + 0x10) = 0;
  ring_advance((int *)(param_1 + 0x2ac));
  list_clear((uint *)(param_1 + 0x2ec));
  preset_path_join((char *)&local_228,param_1,param_2);
  SDMgr_ScanNames((int)DAT_08091d64,(byte *)&local_228,DAT_08091d78,1,(uint *)(param_1 + 0x2ec));
  uVar7 = *(uint *)(param_1 + 0x2ac) & 0xf;
  iVar5 = iVar8 + uVar7 * 0x18;
  *(undefined1 *)(iVar8 + uVar7 * 0x18) = 0x28;
  *(undefined4 *)(iVar5 + 8) = 0;
  *(undefined4 *)(iVar5 + 0xc) = 0;
  *(undefined4 *)(iVar5 + 0x10) = 0;
  ring_advance((int *)(param_1 + 0x2ac));
  PresetMgr_EvtListDone(param_1);
  PresetMgr_EvtFreeSpace(param_1);
  return;
}


