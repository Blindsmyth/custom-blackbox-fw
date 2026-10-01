// 0x0809e218 SessionMgr_LoadBank  (Ghidra decompile, 3.1.9)

/* (app, index, flag): clears pads, then RequestLoad(list[index]) */

void SessionMgr_LoadBank(int param_1,uint param_2,int param_3)

{
  bool bVar1;
  uint uVar2;
  int iVar3;
  ushort uVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  undefined *puVar8;
  int iVar9;
  char local_134 [259];
  undefined1 local_31;
  undefined1 local_30;
  undefined1 local_2f;
  undefined1 local_2e;
  undefined1 local_2d;
  undefined1 local_2c;
  undefined1 local_2b;
  undefined1 local_2a;
  undefined1 local_29 [5];
  
  bVar1 = str_is_empty((undefined4 *)PTR_PresetMgr_pending_0809e4e8);
  if (bVar1) {
    bVar1 = thunk_FUN_0807125c((int)PTR_engine_obj_0809e4ec);
    if (bVar1) {
      if (param_2 < *(uint *)(PTR_PresetMgr_obj_0809e4f0 + 0x32c)) {
        log_printf();
        puVar8 = PTR_engine_obj_0809e4ec;
        iVar6 = 0;
        PTR_engine_obj_0809e4ec[0xa680] = 0;
        FUN_0804c764((int)puVar8);
        pads_clear(param_1);
        FUN_0804c714((int)puVar8);
        do {
          uVar4 = 0;
          do {
            uVar2 = (uint)(short)((ushort)iVar6 | uVar4);
            FUN_0804c664((int)puVar8,
                         uVar2 & 0xf |
                         ((uVar2 & 0x1fff | 0x100) >> 8) << 0x10 | ((uVar2 & 0xff) >> 4) << 8,
                         *(undefined4 *)(&DAT_00008cbc + param_1));
            uVar4 = uVar4 + 0x10;
          } while (uVar4 != 0x40);
          iVar6 = iVar6 + 1;
        } while (iVar6 != 4);
        local_134[0] = '\0';
        bVar1 = list_get(DAT_0809e4f8,param_2,local_134);
        if (bVar1) {
          app_post_cmd((undefined4 *)PTR_PresetMgr_obj_0809e4f0,(int)local_134);
          if (param_3 == 0) {
            FUN_0809e0d0(param_1);
          }
          else {
            str_assign((int *)(&DAT_00008ca0 + param_1),(int)local_134);
            PresetMgr_RequestSaveSettings
                      ((int *)PTR_PresetMgr_obj_0809e4f0,DAT_0809e514,(int)(&DAT_00008c90 + param_1)
                      );
            FUN_0809e0d0(param_1);
          }
        }
        else {
          if (param_3 == 0) {
            FUN_0809e0d0(param_1);
          }
          else {
            str_assign((int *)(&DAT_00008ca0 + param_1),(int)local_134);
            PresetMgr_RequestSaveSettings
                      ((int *)PTR_PresetMgr_obj_0809e4f0,DAT_0809e514,(int)(&DAT_00008c90 + param_1)
                      );
            FUN_0809e0d0(param_1);
          }
          if (param_2 == 0) {
            iVar6 = 0;
            iVar9 = 0;
            do {
              puVar8 = &DAT_00001868 + iVar9 + param_1;
              iVar5 = 0;
              do {
                str_clear((int *)(puVar8 + -0x10));
                *(undefined4 *)(puVar8 + -8) = 0;
                *(undefined4 *)(puVar8 + -4) = 0;
                FUN_08093c98((int)puVar8);
                local_2b = 0x4c;
                FUN_08094a1c((int)(puVar8 + -0x28),&local_2b);
                iVar3 = param_1 + 0x1c00 + iVar6 + iVar5;
                iVar7 = 0;
                do {
                  local_2a = 0x47;
                  FUN_08094a1c(iVar3,&local_2a);
                  FUN_08063cc0((int *)(iVar3 + 0x18));
                  iVar7 = iVar7 + 1;
                  iVar3 = iVar3 + 0x2c;
                } while (iVar7 != 4);
                puVar8 = puVar8 + 0xf0;
                iVar5 = iVar5 + 0x370;
              } while (iVar5 != 0xdc0);
              iVar6 = iVar6 + 0xb0;
              iVar9 = iVar9 + 0x30;
            } while (iVar6 != 0x370);
            puVar8 = &DAT_000029c0 + param_1;
            do {
              FUN_08094be6((int)puVar8);
              puVar8 = puVar8 + 0x30;
            } while (puVar8 != &DAT_00002fc0 + param_1);
            local_2c = 0x26;
            FUN_08094a1c((int)(&DAT_00002fc0 + param_1),&local_2c);
            local_2d = 0x4b;
            FUN_08094a1c(param_1 + 0x2fd8,&local_2d);
            local_2e = 0x36;
            FUN_08094a1c((int)(&DAT_00002ff0 + param_1),&local_2e);
            local_2f = 0;
            FUN_08094a1c(param_1 + 0x3008,&local_2f);
            puVar8 = &DAT_00003078 + param_1;
            do {
              local_29[0] = 1;
              FUN_08093f20((int)puVar8,local_29);
              str_clear((int *)(puVar8 + 0x10));
              *(undefined4 *)(puVar8 + 0x18) = 0;
              *(undefined4 *)(puVar8 + 0x1c) = 0;
              *(undefined4 *)(puVar8 + 0x20) = 0xffff;
              puVar8 = puVar8 + 0x28;
            } while (puVar8 != &DAT_00008a78 + param_1);
            *(undefined4 *)(&DAT_00008a78 + param_1) = DAT_0809e50c;
            local_30 = 0x1f;
            FUN_08093f20((int)(&DAT_00003038 + param_1),&local_30);
            local_31 = 0x4d;
            FUN_08063cc0((int *)(param_1 + 0x3060));
            FUN_08094a1c(param_1 + 0x3048,&local_31);
            FUN_08063ab4(param_1 + 0x8a7c);
            SessionMgr_FinalizeBankLoading(param_1);
            app_new_preset(param_1,DAT_0809e510);
          }
          else {
            *(undefined1 *)(DAT_0809e4fc + 0x680) = 1;
          }
        }
      }
      else {
        log_printf();
      }
    }
    else {
      log_printf();
    }
  }
  else {
    log_printf();
  }
  return;
}


