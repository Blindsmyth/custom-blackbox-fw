// 0x08043dec defaultTask  (Ghidra decompile, 3.1.9)

/* UI task: input -> App vtbl+8 -> draw loop */

void defaultTask(void)

{
  int *piVar1;
  int iVar2;
  undefined4 uVar3;
  undefined4 auStack_50 [16];
  
  (**(code **)(*(int *)*DAT_08043e54 + 4))();
  memset(auStack_50,0,0x40);
  *(undefined1 *)(DAT_08043e58 + 0x8e1) = 1;
  FUN_0808af9c();
  FUN_080443f0();
  log_printf();
  iVar2 = DAT_08043e60;
  piVar1 = DAT_08043e54;
  do {
    FUN_08041b70(iVar2);
    uVar3 = osKernelSysTick();
    FUN_0808b044(*piVar1,uVar3);
    FUN_08042548(iVar2,(undefined2 *)auStack_50);
    (**(code **)(*(int *)*piVar1 + 8))((int *)*piVar1,auStack_50);
    FUN_0808b2d0(*piVar1);
  } while( true );
}


