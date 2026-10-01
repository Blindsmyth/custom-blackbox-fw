// 0x08042e44 SDMgr_FileSize  (Ghidra decompile, 3.1.9)

/* f_stat with 0x120-byte FILINFO; 0 when missing */

undefined8 SDMgr_FileSize(undefined4 param_1,byte *param_2)

{
  undefined8 local_130 [36];
  
  memset((undefined4 *)local_130,0,0x120);
  f_stat(param_2,local_130);
  return local_130[0];
}


