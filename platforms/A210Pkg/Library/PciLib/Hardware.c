// SPDX-License-Identifier: GPL-2.0-only
// Based on Zhihe vendor initialization, Copyright (C) 2025 Zhihe.
// A210 DesignWare host initialization, following the Linux port.
#include "Pci.h"
#include <A210.h>


#define PCIE_CLOCK(Index) (A210_PCIE_CRG + 0x20 + (Index) * 4)
#define PCIE_RESET(Index) (A210_PCIE_CRG + 0x120 + (Index) * 4)
#define PCIE_CLOCK_GATES (BIT0 | BIT4 | BIT8 | BIT12)
#define PCIE_DBI_RESET BIT0
#define PCIE_POWER_RESET BIT4
#define PCIE_AFENCE_RESETS (BIT16 | BIT20)
#define BPC_BYPASS 0x00
#define BPC_POWER_CONTROL 0x04
#define BPC_POWER_REQUESTS (BIT0 | BIT8 | BIT16)
#define PCU_REQUEST 0x08
#define PCU_LOW_POWER_STATE 0x0c
#define PCU_IRQ_ENABLE 0x24
#define PCU_IRQ_CLEAR 0x28
#define PCU_IRQ_STATUS 0x2c
#define PCU_ON 0x1f
#define PCU_IRQ_MASK 0x3f
#define PCU_ACCEPT (BIT0 | BIT3)
#define PCU_REJECT (BIT1 | BIT2 | BIT4 | BIT5)
#define PAD_INPUT BIT9
#define PAD_SCHMITT BIT7
#define PAD_PULL_UP (BIT4 | BIT5)
#define PAD_CONFIG_MASK 0x3ffU
#define PAD_MUX_OFFSET 0x400
#define PAD_PCIE_FUNCTION 5U

STATIC EFI_STATUS
ConfigurePins (CONST VOID *Fdt, INT32 Node)
{
  CONST UINT32 *Groups;
  INT32 Length, Index, Child;
  Groups = FdtGetProp (Fdt, Node, "pinctrl-0", &Length);
  if (!Groups || Length % 4)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Length / 4; Index++)
  {
    UINTN Base, Size;
    INT32 Group = FdtNodeOffsetByPhandle (Fdt, A210Cell (Groups + Index));
    if (EFI_ERROR (A210Reg (Fdt, FdtParentOffset (Fdt, Group),
                            0, &Base, &Size))
        || Base != A210_PERI1_PAD_BASE)
      return EFI_UNSUPPORTED;
    FdtForEachSubnode (Child, Fdt, Group)
    {
      CONST CHAR8 *Pins;
      CONST UINT32 *Drive;
      UINT32 Value = 4; // 7 mA, as specified by the carrier pin groups.
      INT32 Bytes, DriveBytes;
      UINTN Offset = 0;
      Pins = FdtGetProp (Fdt, Child, "pins", &Bytes);
      Drive = FdtGetProp (Fdt, Child, "drive-strength", &DriveBytes);
      if (!Pins || !Drive || DriveBytes != 4 || A210Cell (Drive) != 7)
        return EFI_UNSUPPORTED;
      if (FdtGetProp (Fdt, Child, "input-enable", NULL))
        Value |= PAD_INPUT;
      if (FdtGetProp (Fdt, Child, "input-schmitt-enable", NULL))
        Value |= PAD_SCHMITT;
      if (FdtGetProp (Fdt, Child, "bias-pull-up", NULL))
        Value |= PAD_PULL_UP;
      while (Offset < (UINTN)Bytes)
      {
        UINTN Length = AsciiStrnLenS (Pins + Offset, Bytes - Offset);
        UINTN Pin, Mux;
        if (Length != 8 || Length >= Bytes - Offset
            || CompareMem (Pins + Offset, "GPIO0_", 6)
            || Pins[Offset + 6] < '0' || Pins[Offset + 6] > '3'
            || Pins[Offset + 7] < '0' || Pins[Offset + 7] > '9')
          return EFI_UNSUPPORTED;
        Pin = (Pins[Offset + 6] - '0') * 10 + Pins[Offset + 7] - '0';
        if (Pin < 24 || Pin > 31)
          return EFI_UNSUPPORTED;
        MmioAndThenOr32 (Base + Pin / 2 * 4,
                         ~(PAD_CONFIG_MASK << (Pin % 2 * 16)),
                         Value << (Pin % 2 * 16));
        if (FdtGetProp (Fdt, Child, "function", NULL))
        {
          if (A210PropertyIs (Fdt, Child, "function", "gpio0"))
            Mux = 0;
          else if (A210PropertyIs (Fdt, Child, "function",
                                   Pin < 28 ? "pcie_x1" : "pcie_x4"))
            Mux = PAD_PCIE_FUNCTION;
          else
            return EFI_UNSUPPORTED;
          MmioAndThenOr32 (Base + PAD_MUX_OFFSET + Pin / 8 * 4,
                           ~(0xfU << (Pin % 8 * 4)), Mux << (Pin % 8 * 4));
        }
        Offset += Length + 1;
      }
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
PowerOn (CONST VOID *Fdt, A210_PCIE *Pcie)
{
  INT32 Domain = A210Reference (Fdt, Pcie->Node, "power-domains");
  UINTN Bpc, Pcu, Size, Timeout;
  UINT32 Value;
  if (EFI_ERROR (A210Reg (Fdt, Domain, 0, &Bpc, &Size))
      || EFI_ERROR (A210Reg (Fdt, Domain, 1, &Pcu, &Size))
      || Pcu != 0x0a006000 + Pcie->Index * 0x400 || Bpc != Pcu + 0x200)
    return EFI_UNSUPPORTED;
  MmioWrite32 (Bpc + BPC_BYPASS, 0);
  MmioWrite32 (Bpc + BPC_POWER_CONTROL, BPC_POWER_REQUESTS);
  MmioWrite32 (Pcu + PCU_IRQ_CLEAR, PCU_IRQ_MASK);
  MmioWrite32 (Pcu + PCU_IRQ_ENABLE, PCU_IRQ_MASK);
  MmioWrite32 (Pcu + PCU_LOW_POWER_STATE, PCU_ON);
  MmioWrite32 (Pcu + PCU_REQUEST, 1);
  for (Timeout = 0; Timeout < 10000; Timeout += 10)
  {
    Value = MmioRead32 (Pcu + PCU_IRQ_STATUS);
    if (Value)
    {
      MmioWrite32 (Pcu + PCU_IRQ_CLEAR, Value);
      return !(Value & PCU_REJECT) && (Value & PCU_ACCEPT)
                 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}

STATIC UINTN
PcieCapability (UINTN Dbi)
{
  UINTN Count, Cap = MmioRead8 (Dbi + PCI_CAP_POINTER);
  for (Count = 0; Count < 48 && Cap >= 0x40 && !(Cap & 3); Count++)
  {
    if (MmioRead8 (Dbi + Cap) == PCI_CAP_EXPRESS)
      return Cap;
    Cap = MmioRead8 (Dbi + Cap + 1);
  }
  return 0;
}

EFI_STATUS
A210PcieHardwareInit (CONST VOID *Fdt, A210_PCIE *Pcie)
{
  STATIC CONST CHAR8 *Supplies[] = {
    "vpcie3v3-supply", "vpcie1v5-supply", "vpcie12v-supply"
  };
  INT32 Phy;
  CONST UINT32 *Reset;
  INT32 Length;
  UINTN Index, Cap, Dbi = Pcie->Dbi;
  EFI_STATUS Status;
  Reset = FdtGetProp (Fdt, Pcie->Node, "reset-gpios", &Length);
  if (!Reset || Length != sizeof (Pcie->ResetGpio))
    return EFI_UNSUPPORTED;
  CopyMem (Pcie->ResetGpio, Reset, sizeof (Pcie->ResetGpio));
  Status = ConfigurePins (Fdt, Pcie->Node);
  if (EFI_ERROR (Status))
    return Status;
  Status = A210Gpio (Fdt, Pcie->ResetGpio, TRUE, TRUE);
  if (EFI_ERROR (Status))
    return Status;
  for (Index = 0; Index < ARRAY_SIZE (Supplies); Index++)
  {
    Status = A210Supply (Fdt, Pcie->Node, Supplies[Index]);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
      return Status;
  }
  MmioOr32 (PCIE_CLOCK (Pcie->Index), PCIE_CLOCK_GATES);
  Status = PowerOn (Fdt, Pcie);
  if (EFI_ERROR (Status))
    return Status;
  MmioAnd32 (PCIE_RESET (Pcie->Index), ~PCIE_POWER_RESET);
  MmioOr32 (PCIE_RESET (Pcie->Index), PCIE_DBI_RESET | PCIE_AFENCE_RESETS);
  MmioAndThenOr32 (Pcie->App + A210_PCIE_CONTROL,
                   ~(0x1fU | A210_PCIE_LTSSM), A210_PCIE_ROOT_PORT);
  Phy = A210Reference (Fdt, Pcie->Node, "phys");
  Status = A210PciePhyInit (Fdt, Phy);
  if (EFI_ERROR (Status))
    return Status;
  MmioOr32 (PCIE_RESET (Pcie->Index), PCIE_POWER_RESET);
  MicroSecondDelay (100);
  Cap = PcieCapability (Dbi);
  if (!Cap)
    return EFI_DEVICE_ERROR;
  MmioOr32 (Dbi + DW_DBI_WRITE_ENABLE, BIT0);
  MmioWrite32 (Dbi + DW_DBI2_OFFSET + 0x10, 0);
  MmioWrite32 (Dbi + DW_DBI2_OFFSET + 0x14, 0);
  MmioWrite16 (Dbi + 0x0a, 0x0604);
  MmioAndThenOr32 (Dbi + DW_PORT_LINK,
                   ~((0x3fU << 16) | BIT7),
                   BIT5 | ((Pcie->Lanes * 2 - 1) << 16));
  MmioAndThenOr32 (Dbi + DW_LINK_WIDTH_SPEED,
                   ~(0x1fU << 8), BIT8);
  MmioAndThenOr32 (Dbi + Cap + PCIE_LINK_CAP,
                   ~(0xfU | (0x3fU << 4) | (3U << 10)),
                   Pcie->Speed | (Pcie->Lanes << 4));
  MmioAnd16 (Dbi + Cap + PCIE_LINK_CONTROL, (UINT16)~3U);
  MmioAndThenOr16 (Dbi + Cap + PCIE_LINK_CONTROL2,
                   (UINT16)~0xfU, Pcie->Speed);
  MmioAnd32 (Dbi + DW_DBI_WRITE_ENABLE, ~BIT0);
  MicroSecondDelay (100000);
  Status = A210Gpio (Fdt, Pcie->ResetGpio, TRUE, FALSE);
  if (EFI_ERROR (Status))
    return Status;
  MicroSecondDelay (1000);
  MmioOr32 (Pcie->App + A210_PCIE_CONTROL, A210_PCIE_LTSSM);
  Status = A210Wait32 (Pcie->App + A210_PCIE_LINK_STATUS,
                       A210_PCIE_LINK_UP, A210_PCIE_LINK_UP, 1000000);
  if (EFI_ERROR (Status))
  {
    DEBUG ((DEBUG_WARN, "PCIe%u: no link\n", Pcie->Index));
    return EFI_SUCCESS;
  }
  if ((MmioRead16 (Dbi + Cap + PCIE_LINK_STATUS) & 0xf) < Pcie->Speed)
  {
    MmioOr32 (Dbi + DW_LINK_WIDTH_SPEED, DW_SPEED_CHANGE);
    Status = A210Wait32 (Dbi + DW_LINK_WIDTH_SPEED,
                         DW_SPEED_CHANGE, 0, 1000000);
    if (EFI_ERROR (Status))
      return Status;
  }
  // The request can clear before the link has finished retraining.
  Status = A210Wait32 (Dbi + Cap + PCIE_LINK_CONTROL, BIT27, 0, 1000000);
  if (EFI_ERROR (Status))
    return Status;
  UINT16 Link = MmioRead16 (Dbi + Cap + PCIE_LINK_STATUS);
  DEBUG ((DEBUG_INFO, "PCIe%u: Gen%u x%u\n", Pcie->Index,
           Link & 0xf, (Link >> 4) & 0x3f));
  return EFI_SUCCESS;
}
