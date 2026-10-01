// 0x0808bf90 BitmapFile_SaveToFile  (Ghidra decompile, 3.1.9)

/* screenshot; the only stock FatFs use in the UI task */

void BitmapFile_SaveToFile(int *param_1,byte *param_2)

{
  ushort uVar1;
  undefined4 *puVar2;
  undefined4 *puVar3;
  uint uVar4;
  uint uVar5;
  int iVar6;
  ushort uVar7;
  int local_5c;
  int local_58;
  undefined4 uStack_54;
  undefined4 local_50;
  undefined4 local_4c;
  uint local_48;
  uint local_44;
  undefined2 local_40;
  undefined2 local_3e;
  int local_38;
  undefined4 local_34;
  undefined4 local_30;
  undefined2 local_24 [2];
  
  if ((char)param_1[2] != '\x01') {
    return;
  }
  if ((undefined4 *)*DAT_0808c0a0 != (undefined4 *)0x0) {
    uVar4 = f_open((undefined4 *)*DAT_0808c0a0,param_2,10);
    if (uVar4 == 0) {
      uVar7 = *(ushort *)(param_1 + 1);
      uVar1 = *(ushort *)((int)param_1 + 6);
      iVar6 = (uint)uVar1 * (uint)uVar7 * 3;
      memset(&uStack_54,0,0x30);
      puVar2 = DAT_0808c0a0;
      local_24[0] = 0x4d42;
      local_58 = iVar6 + 0x36;
      local_50 = 0x36;
      local_4c = 0x28;
      local_40 = 1;
      local_3e = 0x18;
      local_34 = 0xb13;
      local_30 = 0xb13;
      local_5c = 0;
      local_48 = (uint)uVar7;
      local_44 = (uint)uVar1;
      local_38 = iVar6;
      f_write((int *)*DAT_0808c0a0,(undefined1 *)local_24,2,&local_5c);
      uVar4 = f_write((int *)*puVar2,(undefined1 *)&local_58,0x34,&local_5c);
      puVar3 = DAT_0808c0a0;
      if (uVar4 == 0) {
        uVar4 = (uint)(short)(*(short *)((int)param_1 + 6) + -1);
        if (-1 < (int)uVar4) {
          uVar5 = 0;
          do {
            if (*(ushort *)(param_1 + 1) != 0) {
              iVar6 = *(ushort *)(param_1 + 1) * uVar4 * 4;
              uVar7 = 0;
              do {
                uVar5 = f_write((int *)*puVar3,(undefined1 *)(*param_1 + iVar6),3,&local_5c);
                uVar7 = uVar7 + 1;
                iVar6 = iVar6 + 4;
              } while (uVar7 < *(ushort *)(param_1 + 1));
            }
            uVar4 = uVar4 - 1;
          } while ((uVar4 & 0x8000) == 0);
          if (uVar5 != 0) {
            f_close((int *)*DAT_0808c0a0);
            log_printf();
            return;
          }
        }
        f_close((int *)*DAT_0808c0a0);
      }
      else {
        f_close((int *)*puVar2);
        log_printf();
      }
    }
    else {
      log_printf();
    }
  }
  return;
}


