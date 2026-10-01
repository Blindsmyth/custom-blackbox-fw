// 0x080905d0 PresetMgr_PopEvent  (Ghidra decompile, 3.1.9)

/* UI side; event 3 success copies +0x390 -> +0x394 */

undefined4 PresetMgr_PopEvent(int param_1,char *param_2)

{
  undefined4 uVar1;
  undefined4 uVar2;
  int iVar3;
  undefined4 uVar4;
  
  if (*(int *)(param_1 + 0x2ac) == *(int *)(param_1 + 0x2a8)) {
    uVar1 = 0;
  }
  else {
    iVar3 = param_1 + (*(uint *)(param_1 + 0x2a8) & 0xf) * 0x18;
    uVar4 = *(undefined4 *)(iVar3 + 300);
    uVar1 = *(undefined4 *)(iVar3 + 0x130);
    uVar2 = *(undefined4 *)(iVar3 + 0x134);
    *(undefined4 *)param_2 = *(undefined4 *)(iVar3 + 0x128);
    *(undefined4 *)(param_2 + 4) = uVar4;
    *(undefined4 *)(param_2 + 8) = uVar1;
    *(undefined4 *)(param_2 + 0xc) = uVar2;
    *(undefined4 *)(param_2 + 0x10) = *(undefined4 *)(iVar3 + 0x138);
    ring_advance((int *)(param_1 + 0x2a8));
    if (*param_2 == '\x03') {
      if (*(int *)(param_2 + 0x10) != 0) {
        str_copy((int *)(param_1 + 0x394),(int *)(param_1 + 0x390));
      }
      str_clear((int *)(param_1 + 0x390));
      uVar1 = 1;
    }
    else {
      uVar1 = 1;
    }
  }
  return uVar1;
}


