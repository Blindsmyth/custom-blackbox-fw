// 0x08091988 PresetMgr_Load  (Ghidra decompile, 3.1.9)

/* cmd 0x22: TryLoad(name); on failure loads the first list row that works */

void PresetMgr_Load(int param_1,char *param_2)

{
  bool bVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  char local_118 [260];
  
  FUN_08042c54(DAT_08091a58);
  *(undefined4 *)(param_1 + 0x2b8) = 1;
  iVar2 = PresetMgr_TryLoad(param_1,param_2);
  if (iVar2 == 0) {
    local_118[0] = '\0';
    uVar4 = 0;
    while (bVar1 = list_get((uint *)(param_1 + 0x32c),uVar4,local_118), bVar1) {
      iVar2 = PresetMgr_TryLoad(param_1,local_118);
      if (iVar2 != 0) {
        str_assign((int *)(param_1 + 0x390),(int)local_118);
        iVar3 = (*(uint *)(param_1 + 0x2ac) & 0xf) * 0x18 + 0x128;
        iVar2 = param_1 + iVar3;
        *(undefined1 *)(param_1 + iVar3) = 3;
        *(undefined4 *)(iVar2 + 8) = 0;
        *(undefined4 *)(iVar2 + 0xc) = 0;
        *(undefined4 *)(iVar2 + 0x10) = 1;
        ring_advance((int *)(param_1 + 0x2ac));
        return;
      }
      uVar4 = uVar4 + 1;
    }
    iVar3 = (*(uint *)(param_1 + 0x2ac) & 0xf) * 0x18 + 0x128;
    iVar2 = param_1 + iVar3;
    *(undefined1 *)(param_1 + iVar3) = 3;
    *(undefined4 *)(iVar2 + 8) = 0;
    *(undefined4 *)(iVar2 + 0xc) = 0;
    *(undefined4 *)(iVar2 + 0x10) = 0;
    ring_advance((int *)(param_1 + 0x2ac));
  }
  else {
    iVar3 = (*(uint *)(param_1 + 0x2ac) & 0xf) * 0x18 + 0x128;
    iVar2 = param_1 + iVar3;
    *(undefined1 *)(param_1 + iVar3) = 3;
    *(undefined4 *)(iVar2 + 8) = 0;
    *(undefined4 *)(iVar2 + 0xc) = 0;
    *(undefined4 *)(iVar2 + 0x10) = 1;
    ring_advance((int *)(param_1 + 0x2ac));
  }
  return;
}


