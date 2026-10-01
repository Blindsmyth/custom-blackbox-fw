// 0x080bd928 preset_screen_key  (Ghidra decompile, 3.1.9)

/* preset screen key handler; case 0x18 = Load */

undefined4 preset_screen_key(int param_1,ushort *param_2)

{
  ushort uVar1;
  uint3 uVar2;
  bool bVar3;
  byte bVar4;
  undefined4 uVar5;
  int iVar6;
  uint uVar7;
  undefined2 local_210;
  undefined4 local_20c;
  undefined2 local_208;
  undefined4 local_204;
  undefined4 local_200;
  undefined4 local_1fc;
  undefined1 local_111;
  uint local_110;
  undefined4 local_10c;
  undefined4 local_108;
  uint local_104;
  undefined4 local_100;
  undefined4 local_fc;
  undefined1 local_11;
  
  uVar1 = *param_2;
  if (uVar1 == 0x139) {
    (&DAT_00001cdf)[param_1] = 0;
    if (0 < *(int *)(param_1 + 0x614)) {
      list_get(DAT_080bde50,*(int *)(param_1 + 0x614) - 1,&DAT_00001cdf + param_1);
    }
    local_110 = local_110 & 0xffffff00;
    bVar3 = list_get(DAT_080bde50,*(uint *)(param_1 + 0x614),(char *)&local_110);
    if (bVar3) {
      SessionMgr_LoadNewPreset(DAT_080bde54,(byte *)&local_110);
    }
    (&DAT_00001e10)[param_1] = 1;
    uVar5 = 0;
    *(undefined2 *)(&DAT_00001f50 + param_1) = 0;
  }
  else {
    uVar2 = local_110._1_3_;
    if (uVar1 < 0x13a) {
      if (uVar1 == 2) {
        uVar7 = *(uint *)(param_2 + 6) & 0xffff;
        if (uVar7 < 0x24) {
          if (uVar7 < 0x18) {
            uVar5 = 1;
          }
          else {
            switch(uVar7) {
            case 0x18:
              local_104 = *(undefined4 *)(param_1 + 0x614);
              local_110 = CONCAT22(local_110._2_2_,(short)*(uint *)(param_2 + 6));
              local_108 = (uint)local_108._2_2_ << 0x10;
              local_10c = DAT_080bdbe8;
              local_100 = 0;
              local_fc = 0;
              ui_post_event_up(param_1,&local_110);
              uVar5 = 0;
              break;
            default:
              uVar5 = 1;
              break;
            case 0x1a:
              if (*(int *)(&DAT_00001bd8 + param_1) != 0) {
                FUN_080c42ac(*(int *)(&DAT_00001bd8 + param_1),DAT_080bdbf4);
              }
              local_110 = CONCAT22(local_110._2_2_,0xd0);
              local_108 = (uint)local_108._2_2_ << 0x10;
              local_10c = DAT_080bdbe8;
              local_104 = 0;
              local_100 = 0;
              local_fc = 0;
              ui_post_event_up(param_1,&local_110);
              (&DAT_00001bde)[param_1] = 1;
              uVar5 = 0;
              break;
            case 0x1c:
              local_210 = local_210 & 0xff00;
              bVar3 = list_get(DAT_080bdbe4,*(uint *)(param_1 + 0x614),(char *)&local_210);
              if (bVar3) {
                strncpy((char *)&local_110,(int)&local_210,0xc);
                local_104 = local_104 & 0xffffff00;
                strncpy((char *)&local_210,(int)&local_110,0xff);
                local_111 = 0;
                local_110 = 0;
                local_10c = 0;
                local_108 = 0;
                local_104 = 0;
                local_100 = 0;
                local_fc = 0;
                snprintf(&local_110,0x17,DAT_080bdbec,&local_210);
                FUN_080a70f0(param_1 + 0x1de0,(byte *)&local_110);
                *(undefined2 *)(&DAT_00001f50 + param_1) = 0x139;
                (&DAT_00001e10)[param_1] = 0;
                FUN_080bd24c(param_1 + 0x6e8,1);
                FUN_080a3e00((int *)(&DAT_000019f8 + param_1),0);
                (**(code **)(*(int *)(param_1 + 0x3ac) + 0x2c))(param_1 + 0x3ac,0);
                (**(code **)(*(int *)(param_1 + 0x404) + 0x2c))(param_1 + 0x404,0);
              }
              uVar5 = 0;
              break;
            case 0x1d:
              local_110 = (uint)uVar2 << 8;
              bVar3 = list_get(DAT_080bdbe4,*(uint *)(param_1 + 0x614),(char *)&local_110);
              if (bVar3) {
                if (*(int *)(&DAT_00001bd8 + param_1) != 0) {
                  FUN_080c42ac(*(int *)(&DAT_00001bd8 + param_1),(int)&local_110);
                }
                strncpy(&DAT_00001bdf + param_1,(int)&local_110,0xff);
                (&DAT_00001cde)[param_1] = 0;
                local_210 = 0xd0;
                local_208 = 0;
                local_20c = DAT_080bdbe8;
                local_204 = 0;
                local_200 = 0;
                local_1fc = 0;
                ui_post_event_up(param_1,&local_210);
                (&DAT_00001bdc)[param_1] = 1;
              }
              uVar5 = 0;
              break;
            case 0x20:
              FUN_08097b44(DAT_080bde54);
              uVar5 = 0;
              break;
            case 0x21:
              local_210 = (ushort)local_210._1_1_ << 8;
              bVar3 = list_get(DAT_080bdbe4,*(uint *)(param_1 + 0x614),(char *)&local_210);
              if (bVar3) {
                if (*(int *)(&DAT_00001bd8 + param_1) != 0) {
                  FUN_080c42ac(*(int *)(&DAT_00001bd8 + param_1),(int)&local_210);
                }
              }
              else {
                local_110 = CONCAT31(local_110._1_3_,bVar3);
                iVar6 = FUN_08097ac8(DAT_080bdbf0,(char *)&local_110);
                if (iVar6 == 0) {
                  strncpy((char *)&local_110,DAT_080bdbf4,0xff);
                  local_11 = 0;
                }
                if (*(int *)(&DAT_00001bd8 + param_1) != 0) {
                  FUN_080c42ac(*(int *)(&DAT_00001bd8 + param_1),DAT_080bdbf4);
                }
              }
              local_110 = CONCAT22(local_110._2_2_,0xd0);
              local_108 = (uint)local_108._2_2_ << 0x10;
              local_10c = DAT_080bdbe8;
              local_104 = 0;
              local_100 = 0;
              local_fc = 0;
              ui_post_event_up(param_1,&local_110);
              (&DAT_00001bdd)[param_1] = 1;
              uVar5 = 0;
              break;
            case 0x22:
              local_110 = (uint)uVar2 << 8;
              bVar3 = list_get(DAT_080bde50,*(uint *)(param_1 + 0x614),(char *)&local_110);
              if (bVar3) {
                FUN_0809e8e4(DAT_080bde54,(byte *)&local_110);
              }
              FUN_080bd24c(param_1 + 0x6e8,1);
              FUN_080a3e00((int *)(&DAT_000019f8 + param_1),0);
              (**(code **)(*(int *)(param_1 + 0x3ac) + 0x2c))(param_1 + 0x3ac,0);
              (**(code **)(*(int *)(param_1 + 0x404) + 0x2c))(param_1 + 0x404,0);
              FUN_080be820((int)(&DAT_00001f54 + param_1),DAT_080bde58);
              uVar5 = 0;
              (&DAT_00001f84)[param_1] = 0;
              break;
            case 0x23:
              FUN_080a70f0(param_1 + 0x1de0,DAT_080bde5c);
              *(undefined2 *)(&DAT_00001f50 + param_1) = 0x14a;
              (&DAT_00001e10)[param_1] = 0;
              FUN_080bd24c(param_1 + 0x6e8,1);
              FUN_080a3e00((int *)(&DAT_000019f8 + param_1),0);
              (**(code **)(*(int *)(param_1 + 0x3ac) + 0x2c))(param_1 + 0x3ac,0);
              (**(code **)(*(int *)(param_1 + 0x404) + 0x2c))(param_1 + 0x404,0);
              uVar5 = 0;
            }
          }
        }
        else if (uVar7 == 0x126) {
          uVar5 = 1;
          (&DAT_00001e10)[param_1] = 1;
          *(undefined2 *)(&DAT_00001f50 + param_1) = 0;
        }
        else if (uVar7 == 0x127) {
          local_210 = (ushort)local_210._1_1_ << 8;
          list_get(DAT_080bde50,*(uint *)(param_1 + 0x614),(char *)&local_210);
          local_110 = local_110 & 0xffffff00;
          FUN_08097ac8(DAT_080bde54,(char *)&local_110);
          iVar6 = strncmp((byte *)&local_210,(byte *)&local_110,0xff);
          if (iVar6 == 0) {
            bVar4 = FUN_08097b84(DAT_080bde54);
            FUN_080a3f0c((int *)(param_1 + 0x1b38),(uint)bVar4,0);
            bVar4 = FUN_08097b84(DAT_080bde54);
            FUN_080a3f0c((int *)(&DAT_00001a98 + param_1),(uint)bVar4,0);
            iVar6 = param_1 + 0x6e8;
            FUN_080bd24c(iVar6,*(byte *)(param_1 + 0x718) ^ 1);
            FUN_080bd566(iVar6,0x21,1);
            uVar7 = FUN_08099d80(DAT_080bde54);
            FUN_080bd566(iVar6,0x22,uVar7);
            bVar4 = FUN_08097b8e(DAT_080bde54);
            uVar7 = (uint)bVar4;
          }
          else {
            FUN_080a3f0c((int *)(param_1 + 0x1b38),1,0);
            FUN_080a3f0c((int *)(&DAT_00001a98 + param_1),0,0);
            iVar6 = param_1 + 0x6e8;
            FUN_080bd24c(iVar6,*(byte *)(param_1 + 0x718) ^ 1);
            FUN_080bd566(iVar6,0x21,1);
            FUN_080bd566(iVar6,0x22,0);
            uVar7 = 0;
          }
          FUN_080bd566(param_1 + 0x6e8,0x23,uVar7);
          FUN_080a3e00((int *)(&DAT_000019f8 + param_1),*(byte *)(param_1 + 0x718) ^ 1);
          (**(code **)(*(int *)(param_1 + 0x3ac) + 0x2c))
                    (param_1 + 0x3ac,*(byte *)(param_1 + 0x718) ^ 1);
          (**(code **)(*(int *)(param_1 + 0x404) + 0x2c))
                    (param_1 + 0x404,*(byte *)(param_1 + 0x718) ^ 1);
          uVar5 = 0;
        }
        else if (uVar7 == 0x7f) {
          local_110 = CONCAT22(local_110._2_2_,0x7f);
          local_108 = (uint)local_108._2_2_ << 0x10;
          local_10c = DAT_080bdbe8;
          local_104 = 0;
          local_100 = 0;
          local_fc = 0;
          ui_post_event_up(param_1,&local_110);
          uVar5 = 0;
        }
        else {
          uVar5 = 1;
        }
      }
      else if (uVar1 == 0x125) {
        (&DAT_00001e10)[param_1] = 1;
        uVar5 = 0;
        *(undefined2 *)(&DAT_00001f50 + param_1) = 0;
      }
      else {
        uVar5 = 1;
      }
    }
    else if (uVar1 == 0x14a) {
      local_110 = (uint)local_110._1_3_ << 8;
      bVar3 = list_get(DAT_080bdbe4,*(uint *)(param_1 + 0x614),(char *)&local_110);
      if (bVar3) {
        FUN_08097ae8(DAT_080bde54,(byte *)&local_110);
      }
      (&DAT_00001e10)[param_1] = 1;
      uVar5 = 0;
      *(undefined2 *)(&DAT_00001f50 + param_1) = 0;
    }
    else {
      uVar5 = 1;
    }
  }
  return uVar5;
}


