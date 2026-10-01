// 0x0804c30c pcmStreamer_loop  (Ghidra decompile, 3.1.9)

void pcmStreamer_loop(uint param_1)

{
  bool bVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  int *piVar5;
  int iVar6;
  ushort uStack_2e;
  undefined4 auStack_2c [2];
  
  iVar6 = param_1 + 0x8258;
  do {
    while( true ) {
      auStack_2c[0] = 0;
      iVar2 = xQueueReceive(*(int **)(&DAT_000047d0 + param_1),auStack_2c,0,0);
      if (iVar2 == 1) break;
      FUN_080469c4(DAT_0807454c);
      FUN_080451dc(iVar6);
      preset_cmd_dispatch(DAT_08074550);
      if (*(int *)(&DAT_00008fac + param_1) != 0) {
        iVar2 = **(int **)(&DAT_00008fa8 + param_1);
        uVar4 = 1;
        if (*(int *)(&DAT_00008fac + param_1) != 1) {
          uVar3 = 1;
          piVar5 = *(int **)(&DAT_00008fa8 + param_1);
          do {
            uVar3 = uVar3 + 1;
            *piVar5 = piVar5[1];
            uVar4 = *(uint *)(&DAT_00008fac + param_1);
            piVar5 = piVar5 + 1;
          } while (uVar3 < uVar4);
        }
        *(uint *)(&DAT_00008fac + param_1) = uVar4 - 1;
        FUN_080741d0(param_1,iVar2);
      }
      FUN_080451ec(iVar6);
      FUN_080451dc(param_1 + 0x8464);
      FUN_080451ec(param_1 + 0x8464);
    }
    FUN_080469c4(DAT_0807454c);
    FUN_080451dc(iVar6);
    while (((*(int *)(&DAT_00004ffc + param_1) != *(int *)(&DAT_00004ff8 + param_1) ||
            (*(int *)(&DAT_00007c4c + param_1) != *(int *)(&DAT_00007c48 + param_1))) ||
           (*(int *)(&DAT_00005804 + param_1) != *(int *)(&DAT_00005800 + param_1)))) {
      FUN_080469c4(DAT_0807454c);
      FUN_080740b4(param_1);
      FUN_08073740(param_1);
      if (*(int *)(&DAT_00005804 + param_1) != *(int *)(&DAT_00005800 + param_1)) {
        iVar2 = param_1 + (*(uint *)(&DAT_00005800 + param_1) & 0x7f) * 0x10;
        if ((&DAT_00005000)[param_1 + (*(uint *)(&DAT_00005800 + param_1) & 0x7f) * 0x10] == '\b') {
          uVar4 = *(uint *)(&DAT_00005000 + iVar2 + 4);
          uVar3 = *(uint *)(&DAT_00005000 + iVar2 + 8);
          uStack_2e = 0;
          bVar1 = FUN_080721f8(param_1,uVar4,uVar3,&uStack_2e,'\0');
          if (bVar1) {
            iVar2 = *(int *)(&DAT_000047cc + param_1);
            *(int *)(&DAT_000047cc + param_1) = iVar2 + 1;
            *(int *)(param_1 + (uint)uStack_2e * 0x1c + 0x10) = iVar2;
          }
          else {
            FUN_08071e7c(param_1,uVar4,uVar3,0xffff);
          }
        }
        ring_advance((int *)(&DAT_00005800 + param_1));
      }
      preset_cmd_dispatch(DAT_08074550);
    }
    preset_cmd_dispatch(DAT_08074550);
    if (*(int *)(&DAT_00008fac + param_1) != 0) {
      iVar2 = **(int **)(&DAT_00008fa8 + param_1);
      uVar4 = 1;
      if (*(int *)(&DAT_00008fac + param_1) != 1) {
        uVar3 = 1;
        piVar5 = *(int **)(&DAT_00008fa8 + param_1);
        do {
          uVar3 = uVar3 + 1;
          *piVar5 = piVar5[1];
          uVar4 = *(uint *)(&DAT_00008fac + param_1);
          piVar5 = piVar5 + 1;
        } while (uVar3 < uVar4);
      }
      *(uint *)(&DAT_00008fac + param_1) = uVar4 - 1;
      FUN_080741d0(param_1,iVar2);
    }
    FUN_080451ec(iVar6);
  } while( true );
}


