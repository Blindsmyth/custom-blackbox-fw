// 0x080918c8 PresetMgr_TryLoad  (Ghidra decompile, 3.1.9)

/* preset.xml via doc_open; legacy root <name>.xml is migrated (writes!) */

int PresetMgr_TryLoad(int param_1,char *param_2)

{
  int iVar1;
  int iVar2;
  byte abStack_310 [256];
  byte abStack_210 [256];
  byte abStack_110 [256];
  
  preset_path_join((char *)abStack_110,param_1,param_2);
  preset_xml_path((char *)abStack_210,param_1,param_2);
  iVar1 = doc_open(abStack_210,*(undefined4 **)(param_1 + 0x2b0),*(uint *)(param_1 + 0x2b4),
                   *(int *)(param_1 + 700));
  if (iVar1 == 0) {
    preset_legacy_xml_path((char *)abStack_310,param_1,(int)param_2);
    iVar1 = doc_open(abStack_310,*(undefined4 **)(param_1 + 0x2b0),*(uint *)(param_1 + 0x2b4),
                     *(int *)(param_1 + 700));
    if (iVar1 != 0) {
      SDMgr_Mkdir(DAT_08091984,abStack_110);
      iVar2 = FUN_08042dac(DAT_08091984,DAT_08091980,2,abStack_110);
      *(int *)(param_1 + 0x2b8) = iVar2;
      iVar2 = doc_save(abStack_210,*(undefined4 *)(param_1 + 0x2b0),*(undefined4 *)(param_1 + 0x2b4)
                       ,*(int *)(param_1 + 700));
      if (iVar2 != 0) {
        FUN_08042e30(DAT_08091984,abStack_310);
        iVar1 = iVar2;
      }
    }
  }
  else {
    iVar2 = FUN_08042dac(DAT_08091984,DAT_08091980,2,abStack_110);
    *(int *)(param_1 + 0x2b8) = iVar2;
  }
  return iVar1;
}


