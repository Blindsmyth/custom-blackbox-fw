// 0x080403c0 Reset_Handler  (Ghidra decompile, 3.1.9)

/* copies .data, zeroes .bss, sets heap, calls main */

void Reset_Handler(void)

{
  bool bVar1;
  char cVar2;
  uint uVar3;
  undefined4 *puVar4;
  undefined4 *puVar5;
  undefined1 *puVar6;
  int iVar7;
  undefined4 *puVar8;
  code *pcVar9;
  
  puVar6 = DAT_08040410;
  SystemInit();
  uVar3 = DAT_e000ed88;
  DAT_e000ed88 = uVar3 | 0xf00000;
  InstructionSynchronizationBarrier(0xf);
  DataSynchronizationBarrier(0xf);
  bVar1 = (bool)isCurrentModePrivileged();
  if (bVar1) {
    setThreadModePrivileged(1);
    bVar1 = (bool)isThreadMode();
    if (bVar1) {
      cVar2 = isUsingMainStack();
      setStackMode(cVar2 == '\x01');
    }
  }
  uVar3 = DAT_e000ef3c;
  DAT_e000ef3c = uVar3 | 0x3000000;
  *DAT_08040414 = DAT_08040418;
  if (DAT_08040504 != DAT_08040508) {
    puVar6 = (undefined1 *)(DAT_08040504 & 0xfffffff8);
  }
  if (DAT_0804050c != DAT_08040510) {
    bVar1 = (bool)isCurrentModePrivileged();
    if (bVar1) {
      setProcessStackPointer(DAT_0804050c & 0xfffffff8);
    }
    bVar1 = (bool)isCurrentModePrivileged();
    if (bVar1) {
      setThreadModePrivileged(1);
      bVar1 = (bool)isThreadMode();
      if (bVar1) {
        cVar2 = isUsingMainStack();
        setStackMode(cVar2 == '\x01');
      }
    }
  }
  FUN_080404c8(DAT_08040514,DAT_08040518,DAT_0804051c);
  FUN_080404c8(DAT_08040520,DAT_08040524,DAT_08040528);
  FUN_080404c8(DAT_0804052c,DAT_08040530,DAT_08040534);
  FUN_080404c8(DAT_08040538,DAT_0804053c,(int)DAT_08040540);
  FUN_080404c8(DAT_08040544,DAT_08040548,DAT_0804054c);
  FUN_080404c8(DAT_08040550,DAT_08040554,DAT_08040558);
  FUN_080404c8(DAT_0804055c,DAT_08040560,DAT_08040564);
  FUN_080404f8(DAT_08040568,DAT_0804056c,0);
  FUN_080404f8(DAT_08040570,DAT_08040574,0);
  puVar4 = DAT_08040578;
  iVar7 = DAT_0804057c - (int)DAT_08040578;
  puVar5 = DAT_0804053c;
  puVar8 = DAT_08040540;
  if (7 < iVar7) {
    *DAT_08040578 = 0;
    puVar4[1] = iVar7;
    puVar5 = DAT_0804053c;
    puVar8 = DAT_08040540;
  }
  while (puVar5 != puVar8) {
    pcVar9 = (code *)*puVar5;
    *(undefined4 **)(puVar6 + -4) = puVar8;
    *(undefined4 **)(puVar6 + -8) = puVar5 + 1;
    (*pcVar9)();
    puVar8 = *(undefined4 **)(puVar6 + -4);
    puVar5 = *(undefined4 **)(puVar6 + -8);
  }
  (*DAT_08040580)(0,0);
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}


