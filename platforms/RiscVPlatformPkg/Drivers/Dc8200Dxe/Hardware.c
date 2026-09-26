// SPDX-License-Identifier: GPL-2.0-only
// DC8200 and DW-HDMI sequences adapted from RevyOS 7.3.y:
// Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
// DW HDMI programming follows Linux dw-hdmi.c and U-Boot dw_hdmi.c.
#include "Display.h"
#include "DwHdmiRegs.h"
#include "vs_crtc_regs.h"
#include "vs_primary_plane_regs.h"
#include "vs_bridge_regs.h"
#include "vs_dc_top_regs.h"


STATIC UINT8 ReadHdmi (DISPLAY_HW *Hw, UINTN Register)
{
  return (UINT8)MmioRead32 (Hw->Hdmi + 4 * Register);
}

STATIC VOID WriteHdmi (DISPLAY_HW *Hw, UINTN Register, UINT8 Value)
{
  MmioWrite32 (Hw->Hdmi + 4 * Register, Value);
}

STATIC VOID ModifyHdmi (DISPLAY_HW *Hw, UINTN Register, UINT8 Mask, UINT8 Value)
{
  WriteHdmi (Hw, Register, (ReadHdmi (Hw, Register) & ~Mask) | Value);
}

STATIC VOID WriteHdmi16 (DISPLAY_HW *Hw, UINTN Register, UINT16 Value)
{
  WriteHdmi (Hw, Register, (UINT8)Value);
  WriteHdmi (Hw, Register + 1, (UINT8)(Value >> 8));
}

STATIC EFI_STATUS WaitI2c (DISPLAY_HW *Hw, UINTN Register)
{
  UINTN Try;
  UINT8 Value;
  for (Try = 0; Try < 100; Try++)
  {
    Value = ReadHdmi (Hw, Register) & 3;
    if (Value != 0)
    {
      WriteHdmi (Hw, Register, Value);
      return (Value & 1) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    MicroSecondDelay (100);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS WritePhy (DISPLAY_HW *Hw, UINT8 Register, UINT16 Value)
{
  WriteHdmi (Hw, HDMI_IH_I2CMPHY_STAT0, 0xff);
  WriteHdmi (Hw, HDMI_PHY_I2CM_ADDRESS_ADDR, Register);
  WriteHdmi (Hw, HDMI_PHY_I2CM_DATAO_1_ADDR, (UINT8)(Value >> 8));
  WriteHdmi (Hw, HDMI_PHY_I2CM_DATAO_0_ADDR, (UINT8)Value);
  WriteHdmi (Hw, HDMI_PHY_I2CM_OPERATION_ADDR, 0x10);
  return WaitI2c (Hw, HDMI_IH_I2CMPHY_STAT0);
}

VOID DisplayDisable (DISPLAY_HW *Hw)
{
  MmioAnd32 (Hw->Dc + VSDC_DISP_PANEL_START, ~BIT (1));
  MmioAnd32 (Hw->Dc + VSDC_DISP_PANEL_CONFIG (1), ~BIT (12));
  MmioOr32 (Hw->Dc + VSDC_DISP_PANEL_CONFIG_EX (1), BIT (0));
  ModifyHdmi (Hw, HDMI_PHY_CONF0, 0x18, 0x10);
  WriteHdmi (Hw, HDMI_MC_CLKDIS, 0x7f);
}

BOOLEAN
DisplayConnected (DISPLAY_HW *Hw)
{
  return (ReadHdmi (Hw, HDMI_PHY_STAT0) & HDMI_PHY_HPD) != 0;
}

EFI_STATUS
DisplayPrepare (DISPLAY_HW *Hw)
{
  UINTN Try;
  EFI_STATUS Status = DisplayPlatformPrepare (Hw);
  if (EFI_ERROR (Status))
    return Status;
  if (MmioRead32 (Hw->Dc + VSDC_TOP_CHIP_MODEL) != 0x8200)
  {
    return EFI_UNSUPPORTED;
  }
  MmioWrite32 (Hw->Dc + VSDC_TOP_IRQ_EN, 0);
  WriteHdmi (Hw, HDMI_IH_MUTE, 3);
  // dw_hdmi_init_hw(): initialize status polarities before polling them.
  WriteHdmi (Hw, HDMI_PHY_I2CM_INT_ADDR, 8);
  WriteHdmi (Hw, HDMI_PHY_I2CM_CTLINT_ADDR, 0x88);
  WriteHdmi (Hw, HDMI_I2CM_SOFTRSTZ, 0);
  WriteHdmi (Hw, HDMI_I2CM_DIV, 0);
  WriteHdmi (Hw, HDMI_I2CM_INT, 8);
  WriteHdmi (Hw, HDMI_I2CM_CTLINT, 0x88);
  WriteHdmi (Hw, HDMI_IH_I2CM_STAT0, 3);
  WriteHdmi (Hw, HDMI_IH_MUTE_I2CM_STAT0, 3);
  WriteHdmi (Hw, HDMI_PHY_POL0, 0xf2);
  WriteHdmi (Hw, HDMI_IH_PHY_STAT0, 0xf1);
  WriteHdmi (Hw, HDMI_PHY_MASK0, 0xff);
  for (Try = 0; Try < 100; Try++)
  {
    if (ReadHdmi (Hw, HDMI_PHY_STAT0) & HDMI_PHY_HPD)
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1000);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
DisplayReadEdid (DISPLAY_HW *Hw)
{
  UINTN Block, Index;
  EFI_STATUS Status;
  Hw->EdidSize = 0;
  // Conservative DDC rate for the 24 MHz HDMI SFR clock.
  WriteHdmi (Hw, HDMI_I2CM_DIV, 0);
  WriteHdmi (Hw, HDMI_I2CM_SS_SCL_HCNT_1_ADDR, 0);
  WriteHdmi (Hw, HDMI_I2CM_SS_SCL_HCNT_0_ADDR, 120);
  WriteHdmi (Hw, HDMI_I2CM_SS_SCL_LCNT_1_ADDR, 0);
  WriteHdmi (Hw, HDMI_I2CM_SS_SCL_LCNT_0_ADDR, 120);
  WriteHdmi (Hw, HDMI_I2CM_SLAVE, 0x50);
  WriteHdmi (Hw, HDMI_I2CM_SEGADDR, 0x30);
  WriteHdmi (Hw, HDMI_I2CM_SEGPTR, 0);
  for (Block = 0; Block < 2; Block++)
  {
    for (Index = 0; Index < 128; Index++)
    {
      WriteHdmi (Hw, HDMI_IH_I2CM_STAT0, 3);
      WriteHdmi (Hw, HDMI_I2CM_ADDRESS, (UINT8)(Block * 128 + Index));
      WriteHdmi (Hw, HDMI_I2CM_OPERATION, 1);
      Status = WaitI2c (Hw, HDMI_IH_I2CM_STAT0);
      if (EFI_ERROR (Status))
      {
        return Status;
      }
      Hw->Edid[Block * 128 + Index] = ReadHdmi (Hw, HDMI_I2CM_DATAI);
    }
    if (!DisplayEdidValid (Hw->Edid + Block * 128))
    {
      return EFI_CRC_ERROR;
    }
    Hw->EdidSize += 128;
    if (Hw->Edid[126] == 0)
    {
      break;
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS PhyEnable (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *T)
{
  STATIC CONST UINT8 Registers[] = {0x06, 0x10, 0x11, 0x0e, 0x09, 0x19};
  STATIC CONST UINT16 Parameters[][ARRAY_SIZE (Registers)] = {
      {0x0001, 0x2080, 0x020a, 0x01a0, 0x8088, 0x0007},
      {0x0002, 0x1142, 0x0214, 0x01a0, 0x8088, 0x0007},
      {0x0003, 0x0285, 0x0228, 0x01a0, 0x8088, 0x0007},
      {0x0003, 0x0283, 0x0628, 0x01a0, 0x8088, 0x0007}};
  UINTN Pass, Index, Try;
  UINTN Row = T->Clock > 90000000 ? 0 :
              T->Clock > 44900000 ? 1 : T->Clock > 35500000 ? 2 : 3;
  EFI_STATUS Status;
  UINT16 Values[ARRAY_SIZE (Registers)];
  for (Index = 0; Index < ARRAY_SIZE (Values); Index++)
    Values[Index] = Parameters[Row][Index];
  DisplayPlatformPhyConfig (Hw, T, Values);
  for (Pass = 0; Pass < 2; Pass++)
  {
    ModifyHdmi (Hw, HDMI_PHY_CONF0, 0x1b, 0x12);
    MicroSecondDelay (5000);
    ModifyHdmi (Hw, HDMI_PHY_CONF0, 0x20, 0x20);
    WriteHdmi (Hw, HDMI_MC_PHYRSTZ, 1);
    WriteHdmi (Hw, HDMI_MC_PHYRSTZ, 0);
    WriteHdmi (Hw, HDMI_MC_HEACPHY_RST, 1);
    ModifyHdmi (Hw, HDMI_PHY_TST0, 0x20, 0x20);
    WriteHdmi (Hw, HDMI_PHY_I2CM_SLAVE_ADDR, 0x69);
    ModifyHdmi (Hw, HDMI_PHY_TST0, 0x20, 0);
    for (Index = 0; Index < ARRAY_SIZE (Registers); Index++)
    {
      Status = WritePhy (Hw, Registers[Index], Values[Index]);
      if (EFI_ERROR (Status))
      {
        return Status;
      }
    }
    ModifyHdmi (Hw, HDMI_PHY_CONF0, 0x18, 8);
    for (Try = 0; Try < 10; Try++)
    {
      if (ReadHdmi (Hw, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK)
      {
        break;
      }
      MicroSecondDelay (1000);
    }
    if (Try == 10)
    {
      return EFI_TIMEOUT;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
DisplaySetTiming (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *T, EFI_PHYSICAL_ADDRESS FrameBuffer)
{
  UINTN Index;
  UINT32 HBlank = T->HFront + T->HSync + T->HBack;
  UINT32 VBlank = T->VFront + T->VSync + T->VBack;
  UINT8 Invid = 0x10 | (T->HPositiveSync ? 0x20 : 0) |
                (T->VPositiveSync ? 0x40 : 0) | (Hw->HdmiSink ? 8 : 0);
  EFI_STATUS Status;
  DisplayDisable (Hw);
  Status = DisplayPlatformPixelClock (Hw, T);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  WriteHdmi (Hw, HDMI_FC_INVIDCONF, Invid);
  WriteHdmi16 (Hw, HDMI_FC_INHACTV0, T->Width);
  WriteHdmi16 (Hw, HDMI_FC_INVACTV0, T->Height);
  WriteHdmi16 (Hw, HDMI_FC_INHBLANK0, HBlank);
  WriteHdmi (Hw, HDMI_FC_INVBLANK, VBlank);
  WriteHdmi16 (Hw, HDMI_FC_HSYNCINDELAY0, T->HFront);
  WriteHdmi16 (Hw, HDMI_FC_HSYNCINWIDTH0, T->HSync);
  WriteHdmi (Hw, HDMI_FC_VSYNCINDELAY, T->VFront);
  WriteHdmi (Hw, HDMI_FC_VSYNCINWIDTH, T->VSync);
  WriteHdmi (Hw, HDMI_FC_CTRLDUR, 12);
  WriteHdmi (Hw, HDMI_FC_EXCTRLDUR, 32);
  WriteHdmi (Hw, HDMI_FC_EXCTRLSPAC, 1);
  WriteHdmi (Hw, HDMI_FC_CH0PREAM, 0x0b);
  WriteHdmi (Hw, HDMI_FC_CH1PREAM, 0x16);
  WriteHdmi (Hw, HDMI_FC_CH2PREAM, 0x21);
  WriteHdmi (Hw, HDMI_TX_INVID0, 1);
  WriteHdmi (Hw, HDMI_TX_INSTUFFING, 7);
  for (Index = HDMI_TX_GYDATA0; Index <= HDMI_TX_BCBDATA1; Index++)
  {
    WriteHdmi (Hw, Index, 0);
  }
  WriteHdmi (Hw, HDMI_VP_PR_CD, 0);
  WriteHdmi (Hw, HDMI_VP_STUFF, 0x27);
  WriteHdmi (Hw, HDMI_VP_REMAP, 0);
  WriteHdmi (Hw, HDMI_VP_CONF, 0x47);
  WriteHdmi (Hw, HDMI_MC_FLOWCTRL, 0);
  WriteHdmi (Hw, HDMI_A_HDCPCFG0, 0);
  WriteHdmi (Hw, HDMI_A_HDCPCFG1, 2);
  WriteHdmi (Hw, HDMI_FC_AVICONF0, 0x60);
  WriteHdmi (Hw, HDMI_FC_AVICONF1,
      T->Width * 3 == T->Height * 4 ? 0x18 :
      T->Width * 9 == T->Height * 16 ? 0x28 : 0x08);
  WriteHdmi (Hw, HDMI_FC_AVICONF2, 8);
  WriteHdmi (Hw, HDMI_FC_AVICONF3, 0);
  WriteHdmi (Hw, HDMI_FC_AVIVID, T->Vic);
  WriteHdmi (Hw, HDMI_FC_PRCONF, 0x10);
  WriteHdmi (Hw, HDMI_MC_CLKDIS, 0x7e);
  WriteHdmi (Hw, HDMI_MC_CLKDIS, 0x7c);

  MmioWrite32 (Hw->Dc + VSDC_DISP_HSIZE (1), T->Width | ((T->Width + HBlank) << 16));
  MmioWrite32 (Hw->Dc + VSDC_DISP_VSIZE (1), T->Height | ((T->Height + VBlank) << 16));
  MmioWrite32 (Hw->Dc + VSDC_DISP_HSYNC (1),
               BIT (30) | (T->HPositiveSync ? 0 : BIT (31)) |
               (T->Width + T->HFront) |
               ((T->Width + T->HFront + T->HSync) << 15));
  MmioWrite32 (Hw->Dc + VSDC_DISP_VSYNC (1),
               BIT (30) | (T->VPositiveSync ? 0 : BIT (31)) |
               (T->Height + T->VFront) |
               ((T->Height + T->VFront + T->VSync) << 15));
  MmioWrite32 (Hw->Dc + VSDC_FB_CONFIG (1), 5U << 26); // Linear XRGB8888.
  MmioWrite32 (Hw->Dc + VSDC_FB_ADDRESS (1), (UINT32)FrameBuffer);
  MmioWrite32 (Hw->Dc + VSDC_FB_STRIDE (1),
               ALIGN_VALUE (T->Width, 32) * 4);
  MmioWrite32 (Hw->Dc + VSDC_FB_SIZE (1), T->Width | (T->Height << 15));
  MmioWrite32 (Hw->Dc + VSDC_FB_TOP_LEFT (1), 0);
  MmioWrite32 (Hw->Dc + VSDC_FB_BOTTOM_RIGHT (1), T->Width | (T->Height << 15));
  MmioWrite32 (Hw->Dc + VSDC_FB_BLEND_CONFIG (1), BIT (1));
  MmioWrite32 (Hw->Dc + VSDC_FB_CONFIG_EX (1), BIT (12) | BIT (13) | BIT (19));
  MmioWrite32 (Hw->Dc + VSDC_DISP_DP_CONFIG (1), 0xa);
  MmioWrite32 (Hw->Dc + VSDC_DISP_PANEL_CONFIG (1), 0x1111);
  MmioAndThenOr32 (Hw->Dc + VSDC_DISP_PANEL_START, ~BIT (3), BIT (1));
  MmioOr32 (Hw->Dc + VSDC_DISP_PANEL_CONFIG_EX (1), 1);
  Status = PhyEnable (Hw, T);
  if (EFI_ERROR (Status))
  {
    DisplayDisable (Hw);
    return Status;
  }
  WriteHdmi (Hw, HDMI_MC_SWRSTZ, 0xfd);
  for (Index = 0; Index < 4; Index++)
  {
    WriteHdmi (Hw, HDMI_FC_INVIDCONF, Invid);
  }
  return EFI_SUCCESS;
}
