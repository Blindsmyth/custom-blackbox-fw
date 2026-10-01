// 0x08086534 f_open  (Ghidra decompile, 3.1.9)

uint f_open(undefined4 *param_1,byte *param_2,uint param_3)

{
  ushort uVar1;
  byte bVar2;
  uint uVar3;
  uint uVar4;
  undefined4 uVar5;
  int iVar6;
  uint uVar7;
  int iVar8;
  bool bVar9;
  uint local_90;
  byte *local_8c [2];
  char *local_84;
  char *local_80;
  byte local_7a;
  byte local_79;
  undefined4 local_78;
  uint local_70;
  uint local_44;
  char local_35;
  undefined4 local_34;
  
  if (param_1 == (undefined4 *)0x0) {
    return 9;
  }
  uVar7 = param_3 & 0x3f;
  local_8c[0] = param_2;
  uVar3 = ff_mount_volume(local_8c,&local_84,uVar7);
  if (uVar3 != 0) goto LAB_0808655a;
  local_80 = local_84;
  local_7a = 0;
  local_44 = uVar3;
  uVar3 = ff_follow_path((int *)&local_80,local_8c[0]);
  if (uVar3 == 0) {
    if (local_35 < '\0') {
      uVar3 = 6;
      goto LAB_0808655a;
    }
    local_90 = local_44;
    if ((param_3 & 0x1c) == 0) {
      if ((local_7a & 0x10) != 0) {
        uVar3 = 4;
        goto LAB_0808655a;
      }
      if (((param_3 & 2) != 0) && ((local_7a & 1) != 0)) {
        uVar3 = 7;
        goto LAB_0808655a;
      }
    }
    else {
      if ((local_7a & 0x11) != 0) {
        uVar3 = 7;
        goto LAB_0808655a;
      }
      if ((param_3 & 4) != 0) {
        uVar3 = 8;
        goto LAB_0808655a;
      }
      if ((param_3 & 8) != 0) goto LAB_080865da;
    }
  }
  else {
    if ((((param_3 & 0x1c) == 0) || (uVar3 != 4)) ||
       (uVar3 = FUN_08085d08((int *)&local_80), uVar3 != 0)) goto LAB_0808655a;
    uVar7 = uVar7 | 8;
LAB_080865da:
    local_90 = local_44;
    bVar2 = local_7a;
    if (*local_84 == '\x04') {
      *param_1 = local_84;
      FUN_080842e4((int)local_84,(int)param_1);
      FUN_08084214((undefined1 *)(*(int *)(local_84 + 0x10) + 2),0,0x1e);
      FUN_08084214((undefined1 *)(*(int *)(local_84 + 0x10) + 0x26),0,0x1a);
      *(undefined1 *)(*(int *)(local_84 + 0x10) + 4) = 0x20;
      iVar8 = *(int *)(local_84 + 0x10);
      uVar5 = FUN_08084174();
      FUN_080841d2((undefined1 *)(iVar8 + 8),uVar5);
      *(undefined1 *)(*(int *)(local_84 + 0x10) + 0x21) = 1;
      local_7a = bVar2;
      local_44 = local_90;
      uVar3 = FUN_08085292((int *)&local_80);
      if (uVar3 != 0) goto LAB_0808655a;
      local_90 = local_44;
      if (param_1[2] != 0) {
        uVar3 = FUN_08084f04(param_1,param_1[2],0);
        uVar4 = param_1[2];
        goto LAB_080867a2;
      }
    }
    else {
      uVar4 = FUN_08086114(local_84,local_44);
      uVar5 = FUN_08084174();
      FUN_080841d2((undefined1 *)(local_90 + 0xe),uVar5);
      *(undefined1 *)(local_90 + 0xb) = 0x20;
      FUN_080848b0(local_84,local_90,0);
      FUN_080841d2((undefined1 *)(local_90 + 0x1c),0);
      local_84[3] = '\x01';
      if (uVar4 != 0) {
        iVar8 = *(int *)(local_84 + 0x38);
        local_7a = bVar2;
        local_44 = local_90;
        uVar3 = FUN_08084f04(&local_80,uVar4,0);
        if (uVar3 != 0) goto LAB_0808655a;
        local_90 = local_44;
        uVar3 = ff_move_window((int)local_84,iVar8);
LAB_080867a2:
        *(uint *)(local_84 + 0x14) = uVar4 - 1;
        if (uVar3 != 0) goto LAB_0808655a;
      }
    }
    uVar7 = uVar7 | 0x40;
  }
  param_1[0x12] = *(undefined4 *)(local_84 + 0x38);
  param_1[0x13] = local_90;
  if (*local_84 == '\x04') {
    param_1[8] = local_78;
    param_1[9] = local_70 & 0xffffff00 | (uint)local_79;
    param_1[10] = local_34;
    FUN_080842e4((int)local_84,(int)param_1);
  }
  else {
    uVar3 = FUN_08086114(local_84,local_90);
    param_1[2] = uVar3;
    uVar5 = FUN_08084178((undefined4 *)(local_90 + 0x1c));
    param_1[4] = uVar5;
    param_1[5] = 0;
  }
  param_1[0x14] = 0;
  *param_1 = local_84;
  *(undefined2 *)(param_1 + 1) = *(undefined2 *)(local_84 + 6);
  *(char *)(param_1 + 0xc) = (char)uVar7;
  *(undefined1 *)((int)param_1 + 0x31) = 0;
  param_1[0x11] = 0;
  param_1[0xe] = 0;
  param_1[0xf] = 0;
  if ((uVar7 & 0x20) == 0) {
    return 0;
  }
  uVar3 = param_1[4];
  iVar8 = param_1[5];
  if (uVar3 == 0 && iVar8 == 0) {
    return 0;
  }
  param_1[0xe] = uVar3;
  param_1[0xf] = iVar8;
  uVar7 = param_1[2];
  uVar1 = *(ushort *)(local_84 + 10);
  uVar4 = (uint)uVar1 * 0x200;
  if ((uint)(uVar3 <= uVar4) <= (uint)-iVar8) {
    do {
      uVar7 = ff_get_fat(param_1,uVar7);
      if (uVar7 < 2) {
        uVar3 = 2;
LAB_0808682e:
        param_1[0x10] = uVar7;
        goto LAB_0808655a;
      }
      if (uVar7 == 0xffffffff) {
        uVar3 = 1;
        goto LAB_0808682e;
      }
      bVar9 = uVar3 < uVar4;
      uVar3 = uVar3 + (uint)uVar1 * -0x200;
      iVar8 = iVar8 - (uint)bVar9;
    } while ((uint)(uVar3 <= uVar4) <= (uint)-iVar8);
  }
  param_1[0x10] = uVar7;
  if ((uVar3 & 0x1ff) == 0) {
    return 0;
  }
  iVar6 = ff_clst2sect((int)local_84,uVar7);
  if (iVar6 != 0) {
    param_1[0x11] = iVar6 + (uVar3 >> 9 | iVar8 << 0x17);
    return 0;
  }
  uVar3 = 2;
LAB_0808655a:
  *param_1 = 0;
  return uVar3;
}


