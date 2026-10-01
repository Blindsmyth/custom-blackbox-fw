// 0x080915a0 preset_path_join  (Ghidra decompile, 3.1.9)

/* dest = Presets + \ + component */

char * preset_path_join(char *param_1,undefined4 param_2,char *param_3)

{
  path_join(param_1,DAT_080915b8,DAT_080915b4,param_3);
  return param_1;
}


