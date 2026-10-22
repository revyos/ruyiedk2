// SPDX-License-Identifier: GPL-2.0-only
// Based on Zhihe vendor initialization, Copyright (C) 2025 Zhihe.
// A210 E16 sequence from the Linux A210 PHY driver.
#include "Pci.h"


#define E16_SYSREG 0x0a180000
#define E16_PHY0 0x0a100000
#define E16_PHY1 0x0a140000
#define E16_GLOBAL 0x00
#define E16_OVERRIDE (BIT16 | BIT20)
#define E16_SPLIT_PCIE BIT8
#define E16_SATA_ENABLE BIT0
#define E16_PORT_SOURCE 0x04
#define E16_SATA_SOURCE (BIT8 | BIT12)
#define E16_PROTOCOL 0x08
#define E16_SATA_PROTOCOL (BIT9 | BIT13)
#define E16_SRAM_STATUS 0x0c
#define E16_SRAM_READY (BIT8 | BIT24)
#define E16_RES_TUNE 0x48
#define E16_RES_ACK_IN BIT16
#define E16_RES_REQUEST BIT0
#define E16_RES_ACK BIT4
#define E16_PCIE_OVERRIDE 0x108
#define E16_PCIE_OVERRIDE_ENABLE (BIT0 | BIT4)
#define E16_MPLL 0x48
#define E16_MPLL_100MHZ 0x350
#define E16_RX_EQ(Lane) (0x4008 + (Lane) * 0x400)
#define E16_RX_EQ_VALUE 0x1f8
#define E16_RX_PLL(Lane) (0xc020 + (Lane) * 0x400)
#define E16_RX_PLL_VALUE 0xf

STATIC EFI_STATUS
ReferenceClock (CONST VOID *Fdt, INT32 Node)
{
  CONST UINT32 *Data;
  INT32 Length, Ref;
  EFI_STATUS Status;
  // The E16 binding describes an APB gate and a 100 MHz reference.
  Data = FdtGetProp (Fdt, Node, "clocks", &Length);
  if (!Data || Length != 12)
    return EFI_UNSUPPORTED;
  Ref = FdtNodeOffsetByPhandle (Fdt, A210Cell (Data + 2));
  if (A210PropertyIs (Fdt, Ref, "compatible", "gpio-gate-clock"))
  {
    Data = FdtGetProp (Fdt, Ref, "enable-gpios", &Length);
    if (!Data || Length != 12)
      return EFI_UNSUPPORTED;
    Status = A210Gpio (Fdt, Data, TRUE, TRUE);
    if (EFI_ERROR (Status))
      return Status;
    Ref = A210Reference (Fdt, Ref, "clocks");
  }
  if (!A210PropertyIs (Fdt, Ref, "compatible", "fixed-clock"))
    return EFI_UNSUPPORTED;
  Data = FdtGetProp (Fdt, Ref, "clock-frequency", &Length);
  return Data && Length == 4 && A210Cell (Data) == 100000000
             ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC EFI_STATUS
AnalogWrite (UINTN Address, UINT32 Value)
{
  MmioWrite32 (Address, Value);
  return MmioRead32 (Address) == Value ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
A210PciePhyInit (CONST VOID *Fdt, INT32 Node)
{
  STATIC BOOLEAN Initialized;
  STATIC CONST UINTN Expected[] = {E16_SYSREG, E16_PHY0, E16_PHY1};
  UINTN Base[3], Size, Index, Lane;
  UINT32 Lanes[4], Control = E16_OVERRIDE;
  BOOLEAN Sata = FALSE;
  CONST UINT32 *Data;
  INT32 Length;
  EFI_STATUS Status;
  if (Initialized)
    return EFI_SUCCESS;
  if (!A210NodeEnabled (Fdt, Node)
      || !A210PropertyIs (Fdt, Node, "compatible", "zhihe,a210-e16-phy"))
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < 3; Index++)
    if (EFI_ERROR (A210Reg (Fdt, Node, Index, &Base[Index], &Size))
        || Base[Index] != Expected[Index])
      return EFI_UNSUPPORTED;
  Data = FdtGetProp (Fdt, Node, "data-lanes", &Length);
  if (!Data || Length != 16)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < 4; Index++)
    Lanes[Index] = A210Cell (Data + Index);
  if (Lanes[0] != 1)
    return EFI_UNSUPPORTED;
  if (Lanes[1] == 1 && Lanes[2] == 1 && Lanes[3] == 1)
  {
    // The V1 carrier routes all four lanes to one root port.
  }
  else if (Lanes[1] == 1 && Lanes[2] == 2 && Lanes[3] == 0)
    Control |= E16_SPLIT_PCIE;
  else if ((Lanes[1] == 1 || Lanes[1] == 2)
           && Lanes[2] == 3 && Lanes[3] == 4)
  {
    Sata = TRUE;
    Control |= E16_SATA_ENABLE;
    if (Lanes[1] == 2)
      Control |= E16_SPLIT_PCIE;
  }
  else
    return EFI_UNSUPPORTED;
  Status = ReferenceClock (Fdt, Node);
  if (EFI_ERROR (Status))
    return Status;
  Status = A210ScmiClockEnable (Fdt, Node, "apb");
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Node, "apb", FALSE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Node, "phy", TRUE);
  if (EFI_ERROR (Status))
    return Status;
  MicroSecondDelay (100);
  MmioWrite32 (Base[0] + E16_GLOBAL, Control);
  MmioWrite32 (Base[0] + E16_PORT_SOURCE, Sata ? E16_SATA_SOURCE : 0);
  MmioWrite32 (Base[0] + E16_PROTOCOL, Sata ? E16_SATA_PROTOCOL : 0);
  MmioWrite32 (Base[0] + E16_PCIE_OVERRIDE, E16_PCIE_OVERRIDE_ENABLE);
  MmioWrite32 (Base[0] + E16_RES_TUNE, E16_RES_ACK_IN);
  Status = A210ScmiReset (Fdt, Node, "phy", FALSE);
  if (EFI_ERROR (Status))
    return Status;
  Status = A210Wait32 (Base[0] + E16_SRAM_STATUS,
                       E16_SRAM_READY, E16_SRAM_READY, 100000);
  if (EFI_ERROR (Status))
    goto Failed;
  for (Index = 1; Index <= 2; Index++)
  {
    Status = AnalogWrite (Base[Index] + E16_MPLL, E16_MPLL_100MHZ);
    if (EFI_ERROR (Status))
      goto Failed;
    for (Lane = 0; Lane < 2; Lane++)
    {
      Status = AnalogWrite (Base[Index] + E16_RX_PLL (Lane),
                             E16_RX_PLL_VALUE);
      if (EFI_ERROR (Status))
        goto Failed;
      Status = AnalogWrite (Base[Index] + E16_RX_EQ (Lane),
                             E16_RX_EQ_VALUE);
      if (EFI_ERROR (Status))
        goto Failed;
    }
  }
  MmioWrite32 (Base[0] + E16_RES_TUNE,
                E16_RES_ACK_IN | E16_RES_REQUEST);
  Status = A210Wait32 (Base[0] + E16_RES_TUNE,
                       E16_RES_ACK, E16_RES_ACK, 100000);
  if (EFI_ERROR (Status))
    goto Failed;
  MicroSecondDelay (200000);
  Initialized = TRUE;
  return EFI_SUCCESS;
Failed:
  (VOID)A210ScmiReset (Fdt, Node, "phy", TRUE);
  DEBUG ((DEBUG_ERROR, "A210 E16 PHY: %r\n", Status));
  return Status;
}
