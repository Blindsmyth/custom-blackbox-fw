// 0x08090b78 app_post_cmd  (Ghidra decompile, 3.1.9)

/* alias of PresetMgr_RequestLoad */

void app_post_cmd(undefined4 *param_1,int param_2)

{
  bool bVar1;
  uint uVar2;
  int *piVar3;
  
  bVar1 = str_is_empty(param_1 + 0xe4);
  if (bVar1) {
    str_assign(param_1 + 0xe4,param_2);
    *(undefined1 *)(param_1 + 1) = 0;
    *param_1 = 0;
    *(undefined1 *)((int)param_1 + 5) = 0;
    piVar3 = param_1 + 2;
    FUN_0804604a(piVar3);
    FUN_0804604a(param_1 + 3);
    str_assign(piVar3,param_2);
    *(undefined1 *)(param_1 + 1) = 0x22;
    uVar2 = param_1[0x49] & 0xf;
    param_1[uVar2 * 4 + 8] = *param_1;
    *(undefined1 *)(param_1 + uVar2 * 4 + 9) = 0x22;
    *(undefined1 *)((int)param_1 + uVar2 * 0x10 + 0x25) = *(undefined1 *)((int)param_1 + 5);
    str_copy(param_1 + uVar2 * 4 + 10,piVar3);
    str_copy(param_1 + uVar2 * 4 + 0xb,param_1 + 3);
    ring_advance(param_1 + 0x49);
    thunk_FUN_080711e0(DAT_08090c14);
  }
  else {
    log_printf();
  }
  return;
}


