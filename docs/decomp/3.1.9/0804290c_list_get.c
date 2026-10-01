// 0x0804290c list_get  (Ghidra decompile, 3.1.9)

/* (list, index, out) -> bool */

bool list_get(uint *param_1,uint param_2,char *param_3)

{
  uint uVar1;
  
  if ((char)param_1[4] != '\0') {
    FUN_0804286c(param_1);
  }
  uVar1 = *param_1;
  if (param_2 < uVar1) {
    strncpy(param_3,*(int *)(param_1[3] + param_2 * 4),0xff);
    param_3[0xff] = '\0';
  }
  return param_2 < uVar1;
}


