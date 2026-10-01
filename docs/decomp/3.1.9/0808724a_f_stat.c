// 0x0808724a f_stat  (Ghidra decompile, 3.1.9)

uint f_stat(byte *param_1,undefined8 *param_2)

{
  uint uVar1;
  byte *local_6c;
  int aiStack_68 [18];
  char local_1d;
  
  local_6c = param_1;
  uVar1 = ff_mount_volume(&local_6c,aiStack_68,0);
  if ((uVar1 == 0) && (uVar1 = ff_follow_path(aiStack_68,local_6c), uVar1 == 0)) {
    if (local_1d < '\0') {
      uVar1 = 6;
    }
    else if (param_2 != (undefined8 *)0x0) {
      ff_get_fileinfo(aiStack_68,param_2);
    }
  }
  return uVar1;
}


