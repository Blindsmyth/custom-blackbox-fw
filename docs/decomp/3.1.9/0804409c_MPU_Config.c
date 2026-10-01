// 0x0804409c MPU_Config  (Ghidra decompile, 3.1.9)

/* 0x24060000/0x24070000 64K non-cacheable, SDRAM 64M, SRAM1-3 512K */

void MPU_Config(void)

{
  undefined4 local_30;
  undefined4 local_2c;
  undefined4 local_28;
  undefined4 local_24;
  
  local_30 = 0;
  local_2c = 0;
  local_28 = 0;
  local_24 = 0;
  FUN_08078c84();
  local_2c = DAT_08044180;
  local_30._0_2_ = 0x401;
  local_28 = 0x300000f;
  local_24 = 0x1000001;
  FUN_08078cc0((byte *)&local_30);
  local_2c = DAT_08044184;
  local_30 = CONCAT22(local_30._2_2_,0x501);
  local_28 = 0x300000f;
  local_24 = 0x1000001;
  FUN_08078cc0((byte *)&local_30);
  local_2c = 0xc0000000;
  local_28._0_1_ = 0x19;
  local_30._0_2_ = CONCAT11(8,(byte)local_30);
  local_24 = 0x1010001;
  FUN_08078cc0((byte *)&local_30);
  local_2c = 0x30000000;
  local_28 = CONCAT31(local_28._1_3_,0x12);
  local_30._0_2_ = CONCAT11(9,(byte)local_30);
  local_24 = 0x1010001;
  FUN_08078cc0((byte *)&local_30);
  FUN_08078ca0(4);
  return;
}


