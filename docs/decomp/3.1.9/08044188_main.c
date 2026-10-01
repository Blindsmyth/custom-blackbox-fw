// 0x08044188 main  (Ghidra decompile, 3.1.9)

/* MPU, drivers, 3x osThreadCreate, osKernelStart */

void main(void)

{
  int iVar1;
  int *piVar2;
  undefined4 *puVar3;
  int iVar4;
  uint *puVar5;
  uint uVar6;
  uint uVar7;
  uint uVar8;
  int local_12c;
  int iStack_128;
  int iStack_124;
  int iStack_120;
  int local_11c;
  int local_118;
  int iStack_114;
  int iStack_110;
  int iStack_10c;
  int local_108;
  int local_104;
  int iStack_100;
  int iStack_fc;
  int iStack_f8;
  int local_f4;
  int aiStack_f0 [59];
  
  FUN_080778d4();
  FUN_08043e84();
  MPU_Config();
  iVar1 = DAT_08044328;
  DataSynchronizationBarrier(0xf);
  InstructionSynchronizationBarrier(0xf);
  *(undefined4 *)(DAT_08044328 + 0x250) = 0;
  *(uint *)(iVar1 + 0x14) = *(uint *)(iVar1 + 0x14) | 0x20000;
  DataSynchronizationBarrier(0xf);
  InstructionSynchronizationBarrier(0xf);
  *(undefined4 *)(iVar1 + 0x84) = 0;
  iVar4 = DAT_08044328;
  DataSynchronizationBarrier(0xf);
  uVar6 = *(uint *)(iVar1 + 0x80);
  uVar8 = ((uVar6 & 0xfffffff) >> 0xd) << 5;
  do {
    uVar7 = (uVar6 & 0x1fff) >> 3;
    do {
      *(uint *)(iVar4 + 0x260) = uVar8 & 0x3fe0 | uVar7 << 0x1e;
      iVar1 = DAT_08044328;
      uVar7 = uVar7 - 1;
    } while (uVar7 != 0xffffffff);
    uVar8 = uVar8 - 0x20;
  } while (uVar8 != 0xffffffe0);
  DataSynchronizationBarrier(0xf);
  *(uint *)(DAT_08044328 + 0x14) = *(uint *)(DAT_08044328 + 0x14) | 0x10000;
  DataSynchronizationBarrier(0xf);
  InstructionSynchronizationBarrier(0xf);
  FUN_08043504(DAT_0804432c);
  FUN_08045e68(DAT_08044330);
  piVar2 = DAT_08044334;
  FUN_08042200();
  FUN_08043cb8(DAT_08044338);
  FUN_08040bcc(DAT_0804433c);
  FUN_08045b00(DAT_08044340);
  FUN_08041a12(piVar2);
  FUN_080419f4(piVar2);
  FUN_08041634(aiStack_f0);
  FUN_080416d8(aiStack_f0);
  FUN_08040aec(DAT_08044344,(int)aiStack_f0);
  iVar4 = FUN_08041c60((int)piVar2);
  FUN_0804c1a4(DAT_08044348,(int)aiStack_f0,iVar4);
  FUN_08044da0();
  FUN_08042c0e(DAT_0804434c);
  puVar5 = FUN_080443a8(DAT_08044350);
  puVar3 = DAT_08044354;
  *DAT_08044354 = puVar5;
  FUN_0808be4c(puVar5);
  *puVar3 = puVar5;
  (**(code **)*puVar5)(puVar5);
  FUN_0807d36c();
  FUN_08046340();
  piVar2 = DAT_08044358;
  local_104 = *DAT_08044358;
  iStack_100 = DAT_08044358[1];
  iStack_fc = DAT_08044358[2];
  iStack_f8 = DAT_08044358[3];
  local_f4 = DAT_08044358[4];
  uVar6 = osThreadCreate(&local_104,0);
  *DAT_0804435c = uVar6;
  local_118 = piVar2[5];
  iStack_114 = piVar2[6];
  iStack_110 = piVar2[7];
  iStack_10c = piVar2[8];
  local_108 = piVar2[9];
  uVar6 = osThreadCreate(&local_118,0);
  *DAT_08044360 = uVar6;
  local_12c = piVar2[10];
  iStack_128 = piVar2[0xb];
  iStack_124 = piVar2[0xc];
  iStack_120 = piVar2[0xd];
  local_11c = piVar2[0xe];
  uVar6 = osThreadCreate(&local_12c,0);
  *DAT_08044364 = uVar6;
  FUN_08046938();
  uVar6 = DAT_e000ed14;
  DAT_e000ed14 = uVar6 | 0x10;
  DataSynchronizationBarrier(0xf);
  InstructionSynchronizationBarrier(0xf);
  *(uint *)(iVar1 + 0x24) = *(uint *)(iVar1 + 0x24) | 0x70000;
  osKernelStart();
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


