// 0x08042c90 SDMgr_CheckMount  (Ghidra decompile, 3.1.9)

/* remounts when the card changed; +0x15 mounted flag */

void SDMgr_CheckMount(undefined4 *param_1)

{
  int iVar1;
  undefined4 uVar2;
  
  if (*(char *)(param_1 + 5) != '\0') {
    FUN_08045b8c(DAT_08042ccc,1);
    *(undefined1 *)(param_1 + 5) = 0;
  }
  iVar1 = FUN_08042c1c();
  if (iVar1 == 0) {
    FUN_08045b34(DAT_08042ccc);
    uVar2 = FUN_08042c1c();
    *(char *)((int)param_1 + 0x15) = (char)uVar2;
    list_clear(param_1);
  }
  else {
    *(undefined1 *)((int)param_1 + 0x15) = 1;
  }
  return;
}


