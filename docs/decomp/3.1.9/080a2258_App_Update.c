// 0x080a2258 App_Update  (Ghidra decompile, 3.1.9)

/* UI pump: RequestList timer, PresetMgr events 3/6/0x0F/0x11/0x13 */

void App_Update(int param_1)

{
  ushort uVar1;
  ushort uVar2;
  undefined4 uVar3;
  int *piVar4;
  uint uVar5;
  char cVar6;
  int iVar7;
  undefined *puVar8;
  int iVar9;
  int iVar10;
  uint uVar11;
  uint uVar12;
  int *piVar13;
  uint uVar14;
  uint local_270;
  undefined4 local_26c;
  undefined2 local_268;
  undefined4 local_264;
  undefined4 local_260;
  undefined4 local_25c;
  char local_258 [4];
  uint local_254;
  int local_250;
  undefined4 uStack_24c;
  int local_248;
  undefined4 local_240;
  undefined4 local_23c;
  undefined4 local_238;
  undefined4 local_234;
  undefined4 local_230;
  undefined4 local_22c;
  char local_228;
  undefined4 local_224;
  undefined4 local_220;
  undefined2 local_21c;
  undefined4 local_218;
  undefined4 uStack_214;
  uint local_210 [6];
  ushort local_1f8 [2];
  undefined4 local_1f4;
  undefined2 local_1f0;
  undefined4 local_1ec;
  undefined4 local_1e8;
  undefined4 local_1e4;
  undefined2 local_1e0 [2];
  undefined4 local_1dc;
  undefined2 local_1d8;
  undefined4 local_1d4;
  undefined4 local_1d0;
  undefined4 local_1cc;
  undefined2 local_1c8 [2];
  undefined4 local_1c4;
  undefined2 local_1c0;
  undefined4 local_1bc;
  undefined4 local_1b8;
  undefined4 local_1b4;
  undefined2 auStack_1b0 [2];
  undefined4 uStack_1ac;
  ushort uStack_1a8;
  undefined4 uStack_1a4;
  undefined4 uStack_1a0;
  undefined4 uStack_19c;
  undefined2 auStack_198 [2];
  undefined4 uStack_194;
  ushort uStack_190;
  undefined4 uStack_18c;
  undefined4 uStack_188;
  undefined4 uStack_184;
  undefined2 auStack_180 [2];
  undefined4 uStack_17c;
  ushort uStack_178;
  undefined4 uStack_174;
  undefined4 uStack_170;
  undefined4 uStack_16c;
  undefined2 auStack_168 [2];
  undefined4 uStack_164;
  ushort uStack_160;
  undefined4 uStack_15c;
  undefined4 uStack_158;
  undefined4 uStack_154;
  undefined4 uStack_150;
  ushort uStack_14c;
  undefined4 uStack_148;
  ushort uStack_144;
  undefined2 auStack_140 [2];
  undefined4 uStack_13c;
  ushort uStack_138;
  undefined4 uStack_134;
  undefined4 uStack_130;
  undefined4 uStack_12c;
  undefined2 auStack_128 [2];
  undefined4 uStack_124;
  ushort uStack_120;
  undefined4 uStack_11c;
  undefined4 uStack_118;
  undefined4 uStack_114;
  undefined2 auStack_110 [2];
  undefined4 uStack_10c;
  ushort uStack_108;
  undefined4 uStack_104;
  undefined4 uStack_100;
  undefined4 uStack_fc;
  undefined2 auStack_f8 [2];
  undefined4 uStack_f4;
  undefined2 uStack_f0;
  int iStack_ec;
  undefined4 uStack_e8;
  undefined4 uStack_e4;
  undefined2 auStack_e0 [2];
  undefined4 uStack_dc;
  undefined2 uStack_d8;
  undefined4 uStack_d4;
  undefined4 uStack_d0;
  undefined4 uStack_cc;
  undefined2 auStack_c8 [2];
  undefined4 uStack_c4;
  undefined2 uStack_c0;
  undefined4 uStack_bc;
  undefined4 uStack_b8;
  undefined4 uStack_b4;
  undefined2 auStack_b0 [2];
  undefined4 uStack_ac;
  undefined2 uStack_a8;
  undefined4 uStack_a4;
  undefined4 uStack_a0;
  undefined4 uStack_9c;
  undefined2 auStack_98 [2];
  undefined4 uStack_94;
  undefined2 uStack_90;
  undefined4 uStack_8c;
  undefined4 uStack_88;
  undefined4 uStack_84;
  undefined2 auStack_80 [2];
  undefined4 uStack_7c;
  undefined2 uStack_78;
  undefined4 uStack_74;
  undefined4 uStack_70;
  undefined4 uStack_6c;
  undefined2 auStack_68 [2];
  undefined4 uStack_64;
  undefined2 uStack_60;
  int iStack_5c;
  undefined4 uStack_58;
  undefined4 uStack_54;
  undefined2 auStack_50 [2];
  undefined4 uStack_4c;
  undefined2 uStack_48;
  int iStack_44;
  undefined4 uStack_40;
  undefined4 uStack_3c;
  undefined2 auStack_38 [2];
  undefined4 uStack_34;
  undefined2 uStack_30;
  undefined4 uStack_2c;
  undefined4 uStack_28;
  undefined4 uStack_24;
  
  FUN_080469ca(DAT_080a23ec);
  if (0 < *(int *)(&DAT_00008cb4 + param_1)) {
    iVar9 = *(int *)(&DAT_00008cb4 + param_1) + -1;
    *(int *)(&DAT_00008cb4 + param_1) = iVar9;
    if (iVar9 == 0x1e) {
      PresetMgr_RequestList(DAT_080a23f8);
      (&DAT_00008cb9)[param_1] = 1;
    }
    iVar9 = DAT_080a23f0;
    local_270 = local_270 & 0xffff0000;
    local_268 = 0;
    local_26c = DAT_080a23f4;
    local_264 = 0;
    local_260 = 0;
    local_25c = 0;
    do {
      iVar10 = FUN_08043a84(iVar9,(undefined2 *)&local_270);
    } while (iVar10 != 0);
    return;
  }
  if ((&DAT_00008ca4)[param_1] == '\x01') {
    if (*(char *)(DAT_080a23fc + 0x15) == '\0') {
      if ((&DAT_0000ea9d)[param_1] == '\0') {
        if ((&DAT_00008cba)[param_1] != '\0') {
          local_1c8[0] = 0xa6;
          local_1c0 = 0;
          local_1c4 = DAT_080a23f4;
          local_1bc = 0;
          local_1b8 = 0;
          local_1b4 = 0;
          ui_post_event(param_1 + 0x30,local_1c8);
          if (((0 < *(int *)(&DAT_00008cb0 + param_1)) &&
              (iVar9 = *(int *)(&DAT_00008cb0 + param_1) + -1,
              *(int *)(&DAT_00008cb0 + param_1) = iVar9, iVar9 == 0)) &&
             ((&DAT_00008cb9)[param_1] == '\0')) {
            PresetMgr_RequestList(DAT_080a23f8);
            (&DAT_00008cb9)[param_1] = 1;
            *(undefined4 *)(&DAT_00008cb0 + param_1) = 0x78;
          }
        }
        goto LAB_080a2280;
      }
    }
    else if ((&DAT_0000ea9d)[param_1] == '\0') {
      if ((&DAT_00008cb8)[param_1] == '\0') {
        PresetMgr_RequestLoadSettings(DAT_080a23f8,DAT_080a2400);
      }
      FUN_0809eaec(param_1,2,0,0);
      (&DAT_00008cb8)[param_1] = 1;
      goto LAB_080a2280;
    }
    FUN_0809eaec(param_1,1,0,0);
    local_1e0[0] = 0xa7;
    local_1d8 = 0;
    local_1dc = DAT_080a23f4;
    local_1d4 = 0;
    local_1d0 = 0;
    local_1cc = 0;
    ui_post_event(param_1 + 0x30,local_1e0);
  }
LAB_080a2280:
  FUN_080437d0();
  iVar9 = DAT_080a23f0;
  local_1f8[0] = 0;
  local_1f0 = 0;
  local_1f4 = DAT_080a23f4;
  local_1ec = 0;
  local_1e8 = 0;
  local_1e4 = 0;
  while (iVar7 = FUN_08043a84(iVar9,local_1f8), iVar10 = DAT_080a2404, iVar7 != 0) {
    App_HandleInput(param_1,local_1f8);
  }
  piVar13 = (int *)(&DAT_0000a678 + DAT_080a2404);
  while (uVar11 = DAT_080a27cc, iVar9 = DAT_080a27c0,
        *(int *)(&DAT_0000a67c + iVar10) != *(int *)(&DAT_0000a678 + iVar10)) {
    uVar11 = *(uint *)(&DAT_0000a678 + iVar10) & 0x7f;
    iVar9 = uVar11 * 0x18 + iVar10;
    local_1f8[0] = *(ushort *)(&DAT_00009000 + iVar9 + 0xa78);
    local_1f0 = *(undefined2 *)(&DAT_00009a80 + iVar10 + uVar11 * 0x18);
    local_1ec = *(undefined4 *)(&DAT_00009000 + iVar9 + 0xa84);
    local_1e8 = *(undefined4 *)(&DAT_00009000 + iVar9 + 0xa88);
    local_1e4 = *(undefined4 *)(&DAT_00009000 + uVar11 * 0x18 + iVar10 + 0xa8c);
    ring_advance(piVar13);
    ui_post_event(param_1 + 0x30,local_1f8);
  }
  local_210[0] = 0;
  local_210[1] = 0;
  local_210[2] = 0;
  local_210[3] = 0;
  local_210[4] = 0;
  local_210[5] = 0;
  while (iVar7 = FUN_0804c748(iVar9,local_210), uVar5 = local_210[3], iVar10 = DAT_080a2a74,
        iVar7 != 0) {
    local_270 = uVar11;
    uVar2 = (ushort)local_210[2];
    uVar1 = uVar2 & 0xff | (ushort)((local_210[2] >> 0x10) << 8) |
            (ushort)(((local_210[2] & 0xffff) >> 8) << 4);
    uVar12 = (uint)uVar1;
    local_26c = CONCAT22(local_26c._2_2_,uVar1);
    uVar14 = local_210[0] & 0xff;
    switch(uVar14) {
    case 0x1d:
    case 0x1e:
      if (((uVar12 & 0xff) >> 4 < 4) && ((uVar2 & 0xf) < 5)) {
        puVar8 = FUN_0809a778(param_1,(int)&local_270);
        puVar8[8] = uVar14 == 0x1d;
        auStack_180[0] = 0x66;
        uStack_17c = uRam080a27c4;
        uStack_174 = 0;
        uStack_170 = 0;
        uStack_16c = 0;
        uStack_178 = uVar1;
        ui_post_event(param_1 + 0x30,auStack_180);
      }
      break;
    case 0x26:
      uStack_150 = uRam080a27c4;
      uStack_14c = uVar1;
      func_0x080935f0(uRam080a27c8);
      FUN_0809d418(param_1,(int)&local_270);
      break;
    case 0x29:
      uStack_148 = uRam080a27c4;
      uStack_144 = uVar1;
      func_0x08093620(uRam080a27c8);
      FUN_0809f4b4(param_1,(int)&local_270);
      break;
    case 0x2b:
      func_0x0809f3a0(param_1,local_210);
      break;
    case 0x2c:
      func_0x0809f3b4(param_1,local_210);
      break;
    case 0x2d:
      FUN_0809f3ec(param_1,(int)local_210);
      break;
    case 0x3f:
      if (((uVar12 & 0xff) >> 4 < 4) && ((uVar2 & 0xf) < 5)) {
        FUN_080992ac(param_1,(int)&local_270,local_210[3]);
      }
      break;
    case 0x4d:
      if (((uVar12 & 0xff) >> 4 < 4) && ((uVar2 & 0xf) < 5)) {
        puVar8 = FUN_0809a778(param_1,(int)&local_270);
        *(uint *)(puVar8 + 4) = uVar5;
        auStack_168[0] = 0x66;
        uStack_164 = uRam080a27c4;
        uStack_15c = 0;
        uStack_158 = 0;
        uStack_154 = 0;
        uStack_160 = uVar1;
        ui_post_event(param_1 + 0x30,auStack_168);
      }
      break;
    case 0x4e:
      FUN_0809d240(param_1,(int)local_210,1);
      auStack_1b0[0] = 0x66;
      uStack_1ac = uRam080a27c4;
      uStack_1a4 = 0;
      uStack_1a0 = 0;
      uStack_19c = 0;
      uStack_1a8 = uVar1;
      ui_post_event(param_1 + 0x30,auStack_1b0);
      break;
    case 0x4f:
      FUN_0809d240(param_1,(int)local_210,0);
      auStack_198[0] = 0x66;
      uStack_194 = uRam080a27c4;
      uStack_18c = 0;
      uStack_188 = 0;
      uStack_184 = 0;
      uStack_190 = uVar1;
      ui_post_event(param_1 + 0x30,auStack_198);
      break;
    case 0x5e:
      FUN_0804c61c(DAT_080a2a74,local_210[2],0,0,0);
      break;
    case 99:
      FUN_080983d0(param_1,(int)&local_270);
      break;
    case 0x66:
      FUN_0809ac60(param_1,(int)local_210);
      break;
    case 0x68:
      FUN_080981b8(param_1,(int)local_210);
      break;
    case 0x69:
      FUN_0809c228(param_1,(int)local_210);
      break;
    case 0x6b:
      SessionMgr_LoadBank(param_1,local_210[3],1);
      break;
    case 0x6f:
      FUN_08097c1c(param_1,(int)local_210);
      break;
    case 0x71:
      FUN_0809a6ac(param_1,(int)local_210);
      break;
    case 0x76:
      if (local_210[3] == 0) {
        auStack_110[0] = 0x38;
        uStack_10c = DAT_080a2a70;
        uStack_104 = 0;
        uStack_100 = 0;
        uStack_fc = 0;
        uStack_108 = uVar1;
        ui_post_event(param_1 + 0x30,auStack_110);
      }
      else {
        auStack_128[0] = 0x37;
        uStack_124 = DAT_080a2a70;
        uStack_11c = 0;
        uStack_118 = 0;
        uStack_114 = 0;
        uStack_120 = uVar1;
        ui_post_event(param_1 + 0x30,auStack_128);
      }
      break;
    case 0x77:
      FUN_080931f4(DAT_080a2a78,&DAT_00008c90 + param_1);
      break;
    case 0x78:
      FUN_08093304((int)DAT_080a2a78);
      break;
    case 0x79:
      FUN_0804c8b4(DAT_080a2a74,0,0,1);
      break;
    case 0x8a:
      if ((uVar1 & 0x1f00) == 0x300) {
        if ((uVar1 & 0xf0) == 0x30) {
          *(bool *)(param_1 + 0x2c) = local_210[4] != 0;
        }
        else {
          *(bool *)(param_1 + 0x2d) = local_210[4] != 0;
        }
        auStack_140[0] = 0x66;
        uStack_13c = DAT_080a2a70;
        uStack_134 = 0;
        uStack_130 = 0;
        uStack_12c = 0;
        uStack_138 = uVar1;
        ui_post_event(param_1 + 0x30,auStack_140);
      }
      break;
    case 0x8d:
      FUN_0809caf0(param_1,(int)local_210);
      break;
    case 0x90:
      FUN_0809b900(param_1,local_210[3]);
    }
  }
  local_228 = '\0';
  local_21c = 0;
  local_218 = 0;
  uStack_214 = 0;
  local_240 = 0;
  local_23c = 0;
  local_238 = 0;
  local_234 = 0;
  local_230 = 0;
  local_22c = 0;
  local_224 = 0;
  local_220 = 0;
  while (iVar7 = FUN_0804c754(iVar10,&local_240), piVar13 = DAT_080a2a78, iVar9 = DAT_080a2a74,
        uVar3 = DAT_080a2a70, iVar7 != 0) {
    if (local_228 == '\x01') {
      FUN_080a02c0(param_1,(char *)&local_240);
    }
  }
  local_258[0] = '\0';
  local_254 = 0xffff;
  local_250 = 0;
  uStack_24c = 0;
  local_248 = 0;
LAB_080a2978:
  do {
    iVar7 = thunk_FUN_080711fc(iVar9,(undefined4 *)local_258);
    iVar10 = DAT_080a2a7c;
    piVar4 = DAT_080a2a78;
    if (iVar7 == 0) break;
    switch(local_258[0]) {
    case '\x0e':
      goto code_r0x080a296c;
    case '\x1b':
      FUN_0809d998(param_1,local_254);
      goto LAB_080a2978;
    case '\x1c':
      auStack_f8[0] = 0x136;
      uStack_f0 = 0;
      uStack_f4 = uVar3;
      iStack_ec = local_248;
      uStack_e8 = 0;
      uStack_e4 = 0;
      ui_post_event(param_1 + 0x30,auStack_f8);
      goto LAB_080a2978;
    case '\x1d':
      auStack_e0[0] = 0x137;
      uStack_d8 = 0;
      uStack_dc = uVar3;
      uStack_d4 = 0;
      uStack_d0 = 0;
      uStack_cc = 0;
      ui_post_event(param_1 + 0x30,auStack_e0);
      goto LAB_080a2978;
    case ')':
      func_0x08093588(piVar13,local_254);
      goto LAB_080a2978;
    }
  } while( true );
LAB_080a2a96:
  do {
    iVar9 = PresetMgr_PopEvent(iVar10,local_258);
    if (iVar9 == 0) {
      FUN_08040724(DAT_080a2da8);
      iVar9 = DAT_080a2da8;
      piVar13 = (int *)(&DAT_00001270 + DAT_080a2da8);
      while (*(int *)(&DAT_00001274 + iVar9) != *(int *)(&DAT_00001270 + iVar9)) {
        iVar10 = iVar9 + (*(uint *)(&DAT_00001270 + iVar9) & 0x3f) * 0x18;
        local_1f8[0] = *(ushort *)(iVar10 + 0xc70);
        local_1f0 = *(undefined2 *)(iVar10 + 0xc78);
        local_1ec = *(undefined4 *)(iVar10 + 0xc7c);
        local_1e8 = *(undefined4 *)(iVar10 + 0xc80);
        local_1e4 = *(undefined4 *)
                     (iVar9 + (*(uint *)(&DAT_00001270 + iVar9) & 0x3f) * 0x18 + 0xc84);
        ring_advance(piVar13);
        FUN_080a1718(param_1,local_1f8);
      }
      while (iVar9 = FUN_080b5798(param_1 + 0xc38,local_1f8), iVar9 != 0) {
        if (local_1f8[0] == 0x60) {
          cVar6 = FUN_0804ca98(DAT_080a2dac);
          if (cVar6 == '\0') {
            FUN_0804c8b4(DAT_080a2dac,1,0,1);
          }
          else {
            FUN_0804c8b4(DAT_080a2dac,1,0,0);
          }
        }
      }
      if (*(int *)(&DAT_0000eaa0 + param_1) < 1) {
        return;
      }
      iVar9 = *(int *)(&DAT_0000eaa0 + param_1) + -1;
      *(int *)(&DAT_0000eaa0 + param_1) = iVar9;
      if (iVar9 != 0) {
        return;
      }
      FUN_0809c2c8(param_1);
      return;
    }
    switch(local_258[0]) {
    case '\x03':
      goto code_r0x080a2a80;
    case '\x06':
      func_0x08090e54(iVar10,&DAT_00008c90 + param_1);
      settings_load(param_1);
      goto LAB_080a2a96;
    case '\x0f':
      *(int *)(&DAT_0000e988 + param_1) = local_250;
      *(undefined4 *)(&DAT_0000e98c + param_1) = uStack_24c;
      auStack_80[0] = 0x66;
      uStack_78 = 0x40;
      uStack_7c = DAT_080a2da4;
      uStack_74 = 0;
      uStack_70 = 0;
      uStack_6c = 0;
      ui_post_event(param_1 + 0x30,auStack_80);
      goto LAB_080a2a96;
    case '\x11':
      (&DAT_00008cba)[param_1] = 1;
      if (*(char *)(iRam080a2da0 + 0x15) == '\0') {
        FUN_0809eaec(param_1,1,0,0);
        auStack_c8[0] = 0xa6;
        uStack_c0 = 0;
        uStack_c4 = DAT_080a2da4;
        uStack_bc = 0;
        uStack_b8 = 0;
        uStack_b4 = 0;
        ui_post_event(param_1 + 0x30,auStack_c8);
        *(undefined4 *)(&DAT_00008cb0 + param_1) = 0x78;
      }
      else {
        auStack_b0[0] = 0x69;
        uStack_a8 = 0;
        uStack_ac = DAT_080a2da4;
        uStack_a4 = 0;
        uStack_a0 = 0;
        uStack_9c = 0;
        ui_post_event(param_1 + 0x30,auStack_b0);
      }
      (&DAT_00008cb9)[param_1] = 0;
      goto LAB_080a2a96;
    case '\x13':
      auStack_98[0] = 0x69;
      uStack_90 = 0;
      uStack_94 = DAT_080a2da4;
      uStack_8c = 0;
      uStack_88 = 0;
      uStack_84 = 0;
      ui_post_event(param_1 + 0x30,auStack_98);
      goto LAB_080a2a96;
    case '\x1a':
      FUN_08098944(param_1);
      goto LAB_080a2a96;
    case '%':
      auStack_68[0] = 0x147;
      uStack_60 = 0;
      uStack_64 = DAT_080a2da4;
      iStack_5c = local_248;
      uStack_58 = 0;
      uStack_54 = 0;
      ui_post_event(param_1 + 0x30,auStack_68);
      goto LAB_080a2a96;
    case '&':
      auStack_50[0] = 0x148;
      uStack_48 = 0;
      uStack_4c = DAT_080a2da4;
      iStack_44 = local_248;
      uStack_40 = 0;
      uStack_3c = 0;
      ui_post_event(param_1 + 0x30,auStack_50);
      (&DAT_00008cc3)[param_1] = 0;
      goto LAB_080a2a96;
    case '\'':
      auStack_38[0] = 0x149;
      uStack_30 = 0;
      uStack_34 = DAT_080a2da4;
      uStack_2c = 0;
      uStack_28 = 0;
      uStack_24 = 0;
      ui_post_event(param_1 + 0x30,auStack_38);
      (&DAT_00008cc5)[param_1] = 0;
      goto LAB_080a2a96;
    case '(':
      FUN_08099e60(param_1);
      goto LAB_080a2a96;
    case '*':
      func_0x08093578(piVar4);
      goto LAB_080a2a96;
    }
  } while( true );
code_r0x080a2a80:
  if (local_248 == 0) {
    app_new_preset(param_1,iRam080a2d9c);
  }
  else {
    func_0x08090c9c(iVar10,&DAT_00001840 + param_1);
  }
  SessionMgr_FinalizeBankLoading(param_1);
  goto LAB_080a2a96;
code_r0x080a296c:
  FUN_080a121c(param_1,local_254,local_250,uStack_24c);
  goto LAB_080a2978;
}


