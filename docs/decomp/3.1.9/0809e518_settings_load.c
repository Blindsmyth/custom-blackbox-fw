// 0x0809e518 settings_load  (Ghidra decompile, 3.1.9)

/* startup settings.tml loader; auto-loads last preset */

void settings_load(int param_1)

{
  int iVar1;
  uint *puVar2;
  bool bVar3;
  byte *pbVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  uint in_fpscr;
  float fVar8;
  byte local_140 [256];
  int local_40;
  ushort local_3a;
  ushort auStack_38 [2];
  undefined4 uStack_34;
  undefined2 uStack_30;
  int iStack_2c;
  undefined4 uStack_28;
  undefined4 uStack_24;
  
  iVar5 = DAT_0809e760;
  uVar7 = 0;
  local_3a = 0;
  local_40 = 0;
  while (bVar3 = FUN_080947ec((int)(&DAT_00008c90 + param_1),uVar7,(short *)&local_3a,&local_40),
        bVar3) {
    uVar7 = uVar7 + 1;
    FUN_0804c59c(iVar5,0x400,(uint)local_3a,local_40,0,0);
    iVar1 = DAT_0809e760;
    uVar6 = (uint)local_3a;
    if (uVar6 < 0x97) {
      if (0x70 < uVar6) {
        switch(uVar6) {
        case 0x71:
          PTR_DAT_0809e77c[0x688] = (char)local_40;
          break;
        case 0x7b:
          (&DAT_0000a68a)[DAT_0809e760] = local_40 != 0;
          FUN_0804d504(iVar1);
          break;
        case 0x7f:
          fVar8 = (float)VectorSignedToFloat(local_40,(byte)(in_fpscr >> 0x16) & 3);
          FUN_080423e4(fVar8 / fRam0809e768,iRam0809e76c);
          break;
        case 0x80:
          fVar8 = (float)VectorSignedToFloat(local_40,(byte)(in_fpscr >> 0x16) & 3);
          FUN_080423ec(fVar8 / fRam0809e768,iRam0809e76c);
          break;
        case 0x96:
          auStack_38[0] = 0x158;
          uStack_30 = 0;
          uStack_34 = uRam0809e770;
          iStack_2c = local_40;
          uStack_28 = 0;
          uStack_24 = 0;
          ui_post_event(param_1 + 0x30,auStack_38);
          bVar3 = local_40 == 0;
          *(bool *)(iRam0809e76c + 0x198) = bVar3;
          PTR_DAT_0809e774[0x9ce] = bVar3;
          PTR_DAT_0809e778[0xc14] = bVar3;
        }
      }
    }
    else if (uVar6 < 0x111) {
      if (uVar6 < 0x10f) {
        if (uVar6 == 0xf0) {
          (&DAT_0000a689)[DAT_0809e760] = (char)local_40;
          FUN_0804d504(iVar1);
        }
      }
      else {
        FUN_0804c59c(DAT_0809e760,0x10400,uVar6,local_40,0,0);
      }
    }
    else if (uVar6 == 0x19b) {
      *(bool *)(DAT_0809e764 + 0x17) = local_40 != 0;
    }
  }
  bVar3 = str_is_empty((undefined4 *)(&DAT_00008ca0 + param_1));
  puVar2 = DAT_0809e780;
  if (!bVar3) {
    uVar7 = 0;
    local_140[0] = bVar3;
    while (bVar3 = list_get(puVar2,uVar7,(char *)local_140), bVar3) {
      pbVar4 = (byte *)str_cstr((int *)(&DAT_00008ca0 + param_1));
      if (pbVar4 == (byte *)0x0) {
        iVar5 = strnlen((char *)local_140,0x100);
      }
      else {
        iVar5 = strncmp(local_140,pbVar4,0xff);
      }
      if (iVar5 == 0) {
        SessionMgr_LoadBank(param_1,uVar7,0);
        return;
      }
      uVar7 = uVar7 + 1;
    }
  }
  SessionMgr_LoadBank(param_1,0,1);
  return;
}


