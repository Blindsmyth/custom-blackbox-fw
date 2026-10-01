// 0x08091860 preset_xml_path  (Ghidra decompile, 3.1.9)

/* joins preset.xml onto the preset folder */

char * preset_xml_path(char *param_1,undefined4 param_2,char *param_3)

{
  char acStack_110 [260];
  
  preset_path_join(acStack_110,param_2,param_3);
  path_join(param_1,DAT_08091884,(int)acStack_110,DAT_08091880);
  return param_1;
}


