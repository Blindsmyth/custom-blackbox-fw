// 0x08042f64 SDMgr_ScanNames  (Ghidra decompile, 3.1.9)

/* (sd, dir, pattern, dirs_only, list): dir entries stored as \name */

void SDMgr_ScanNames(int param_1,byte *param_2,byte *param_3,int param_4,uint *param_5)

{
  int iVar1;
  int iVar2;
  uint uVar3;
  char acStack_3a0 [255];
  undefined1 local_2a1;
  char local_2a0 [255];
  undefined1 local_1a1;
  undefined8 uStack_1a0;
  byte local_194;
  char acStack_189 [265];
  int aiStack_80 [23];
  
  list_clear(param_5);
  if (*(char *)(param_1 + 0x15) == '\0') {
    log_printf();
  }
  else {
    iVar1 = strncmp(param_3,DAT_08043108,5);
    memset(aiStack_80,0,0x58);
    memset((undefined4 *)&uStack_1a0,0,0x120);
    local_2a0[0] = '\0';
    iVar2 = f_findfirst(aiStack_80,&uStack_1a0,param_2,(int)param_3);
    if (iVar2 == 0) {
      while (acStack_189[3] != '\0') {
        if (acStack_189[3] == '.') {
          local_2a0[0] = '\0';
        }
        else {
          strncpy(local_2a0,(int)(acStack_189 + 3),0xff);
          local_1a1 = 0;
          iVar2 = strnlen(local_2a0,0x100);
          if ((((3 < iVar2) && (iVar1 == 0)) && (acStack_189[iVar2] == 'W')) &&
             ((acStack_189[iVar2 + 1] == 'A' && (acStack_189[iVar2 + 2] == 'V')))) {
            FUN_08042e78(acStack_3a0,param_1,(int)local_2a0,DAT_08043110);
            strncpy(local_2a0,(int)acStack_3a0,0xff);
          }
          if (local_2a0[0] != '\0') {
            list_add((int *)param_5,(int)local_2a0);
          }
        }
        if ((param_5[1] <= *param_5) || (uVar3 = f_findnext(aiStack_80,&uStack_1a0), uVar3 != 0))
        break;
      }
    }
    f_closedir(aiStack_80);
    memset(aiStack_80,0,0x58);
    if (param_4 != 0) {
      iVar2 = f_findfirst(aiStack_80,&uStack_1a0,param_2,DAT_08043114);
      iVar1 = DAT_08043118;
      if (iVar2 == 0) {
        while (acStack_189[3] != '\0') {
          local_2a0[0] = '\0';
          if (((local_194 & 0x12) == 0x10) && (acStack_189[3] != '.')) {
            strncpy(local_2a0,iVar1,0xff);
            local_1a1 = 0;
            strncpy(acStack_3a0,(int)local_2a0,0xff);
            local_2a1 = 0;
            strncat(acStack_3a0,acStack_189 + 3,0xff);
            strncpy(local_2a0,(int)acStack_3a0,0xff);
            if (local_2a0[0] != '\0') {
              list_add((int *)param_5,(int)local_2a0);
            }
          }
          if ((param_5[1] <= *param_5) || (uVar3 = f_findnext(aiStack_80,&uStack_1a0), uVar3 != 0))
          break;
        }
      }
      f_closedir(aiStack_80);
    }
  }
  return;
}


