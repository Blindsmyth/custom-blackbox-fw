// 0x08086a06 f_write  (Ghidra decompile, 3.1.9)

uint f_write(int *param_1,undefined1 *param_2,uint param_3,int *param_4)

{
  char *pcVar1;
  char *pcVar2;
  uint uVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  uint uVar9;
  undefined8 uVar10;
  char *local_2c [2];
  
  *param_4 = 0;
  uVar3 = ff_validate(param_1,local_2c);
  pcVar1 = local_2c[0];
  if ((uVar3 == 0) && (uVar3 = (uint)*(byte *)((int)param_1 + 0x31), uVar3 == 0)) {
    if ((*(byte *)(param_1 + 0xc) & 2) == 0) {
      uVar3 = 7;
    }
    else {
      if ((*local_2c[0] != '\x04') && (CARRY4(param_1[0xe],param_3))) {
        param_3 = ~param_1[0xe];
      }
      while (param_3 != 0) {
        uVar6 = param_1[0xe];
        uVar7 = param_1[0xf];
        if ((uVar6 & 0x1ff) == 0) {
          uVar9 = *(ushort *)(pcVar1 + 10) - 1 & (uVar6 >> 9 | uVar7 << 0x17);
          if (uVar9 == 0) {
            if (uVar6 == 0 && uVar7 == 0) {
              uVar6 = param_1[2];
              if (uVar6 == 0) {
                uVar6 = FUN_08084ce8(param_1,0);
                goto LAB_08086a64;
              }
            }
            else {
              if (param_1[0x14] == 0) {
                uVar6 = FUN_08084ce8(param_1,param_1[0x10]);
              }
              else {
                uVar10 = FUN_0808425a(param_1,param_1[0x14],uVar6,uVar7);
                uVar6 = (uint)uVar10;
              }
LAB_08086a64:
              if (uVar6 == 0) break;
            }
            if (uVar6 == 1) {
              *(undefined1 *)((int)param_1 + 0x31) = 2;
              return 2;
            }
            if (uVar6 == 0xffffffff) {
              *(undefined1 *)((int)param_1 + 0x31) = 1;
              return 1;
            }
            param_1[0x10] = uVar6;
            if (param_1[2] == 0) {
              param_1[2] = uVar6;
            }
          }
          if ((*(int *)(pcVar1 + 0x38) == param_1[0x11]) &&
             (iVar4 = ff_sync_window((int)pcVar1), iVar4 != 0)) {
            *(undefined1 *)((int)param_1 + 0x31) = 1;
            return 1;
          }
          iVar4 = ff_clst2sect((int)pcVar1,param_1[0x10]);
          if (iVar4 == 0) {
            *(undefined1 *)((int)param_1 + 0x31) = 2;
            return 2;
          }
          iVar4 = iVar4 + uVar9;
          uVar6 = param_3 >> 9;
          if (uVar6 == 0) {
            if ((uint)param_1[5] < (uint)param_1[0xf] ||
                (uint)(param_1[0xf] - param_1[5]) < (uint)((uint)param_1[4] <= (uint)param_1[0xe]))
            {
              iVar5 = ff_sync_window((int)pcVar1);
              if (iVar5 != 0) {
                *(undefined1 *)((int)param_1 + 0x31) = 1;
                return 1;
              }
              *(int *)(pcVar1 + 0x38) = iVar4;
            }
            param_1[0x11] = iVar4;
            goto LAB_08086b44;
          }
          if ((uint)*(ushort *)(pcVar1 + 10) < uVar9 + uVar6) {
            uVar6 = *(ushort *)(pcVar1 + 10) - uVar9;
          }
          iVar5 = disk_write((uint)(byte)pcVar1[1]);
          if (iVar5 != 0) {
            *(undefined1 *)((int)param_1 + 0x31) = 1;
            return 1;
          }
          if ((uint)(*(int *)(pcVar1 + 0x38) - iVar4) < uVar6) {
            FUN_08084200((int)(pcVar1 + 0x3c),param_2 + (*(int *)(pcVar1 + 0x38) - iVar4) * 0x200,
                         0x200);
            pcVar1[3] = '\0';
          }
          uVar6 = uVar6 << 9;
        }
        else {
LAB_08086b44:
          pcVar2 = local_2c[0];
          uVar7 = param_1[0xe];
          uVar6 = ff_move_window((int)local_2c[0],param_1[0x11]);
          if (uVar6 != 0) {
            *(undefined1 *)((int)param_1 + 0x31) = 1;
            return 1;
          }
          uVar6 = 0x200 - (uVar7 & 0x1ff);
          if (param_3 <= uVar6) {
            uVar6 = param_3;
          }
          FUN_08084200((int)(pcVar2 + (param_1[0xe] & 0x1ffU) + 0x3c),param_2,uVar6);
          pcVar2[3] = '\x01';
        }
        param_3 = param_3 - uVar6;
        *param_4 = *param_4 + uVar6;
        param_2 = param_2 + uVar6;
        uVar8 = uVar6 + param_1[0xe];
        uVar9 = param_1[0xf] + (uint)CARRY4(uVar6,param_1[0xe]);
        param_1[0xe] = uVar8;
        param_1[0xf] = uVar9;
        uVar7 = param_1[5];
        uVar6 = param_1[4];
        if (uVar7 <= uVar9 && (uint)(uVar8 <= (uint)param_1[4]) <= uVar7 - uVar9) {
          uVar6 = uVar8;
          uVar7 = uVar9;
        }
        param_1[4] = uVar6;
        param_1[5] = uVar7;
      }
      *(byte *)(param_1 + 0xc) = *(byte *)(param_1 + 0xc) | 0x40;
    }
  }
  return uVar3;
}


