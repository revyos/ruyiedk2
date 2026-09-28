// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Pci.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>
#include <Protocol/PciRootBridgeIo.h>

STATIC A210_PCIE mPcie[2];
STATIC struct {
  ACPI_HID_DEVICE_PATH Acpi;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath[2] = {
  {{{ACPI_DEVICE_PATH, ACPI_DP, {sizeof (ACPI_HID_DEVICE_PATH), 0}},
    EISA_PNP_ID (0x0a08), 0},
   {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}},
  {{{ACPI_DEVICE_PATH, ACPI_DP, {sizeof (ACPI_HID_DEVICE_PATH), 0}},
    EISA_PNP_ID (0x0a08), 1},
   {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}}
};

EFI_STATUS
A210PcieAtu (A210_PCIE *Pcie, UINTN Region, UINT32 Type,
             UINT64 Cpu, UINT64 Pci, UINT64 Size)
{
  UINTN Base = Pcie->Dbi + DW_IATU_OFFSET + Region * DW_IATU_STRIDE;
  if (!Size || Cpu > MAX_UINT64 - Size || Pci > MAX_UINT64 - Size
      || ((Cpu | Pci | Size) & (A210_IATU_ALIGNMENT - 1))
      || (Cpu >> 32) != ((Cpu + Size - 1) >> 32))
    return EFI_UNSUPPORTED;
  MmioWrite32 (Base + DW_IATU_CONTROL2, 0);
  MmioWrite32 (Base + DW_IATU_BASE_LO, Cpu);
  MmioWrite32 (Base + DW_IATU_BASE_HI, Cpu >> 32);
  MmioWrite32 (Base + DW_IATU_LIMIT, Cpu + Size - 1);
  MmioWrite32 (Base + DW_IATU_TARGET_LO, Pci);
  MmioWrite32 (Base + DW_IATU_TARGET_HI, Pci >> 32);
  MmioWrite32 (Base + DW_IATU_CONTROL1, Type);
  MmioWrite32 (Base + DW_IATU_CONTROL2, DW_IATU_ENABLE);
  return A210Wait32 (Base + DW_IATU_CONTROL2,
                     DW_IATU_ENABLE, DW_IATU_ENABLE, 10000);
}

STATIC EFI_STATUS
Ranges (CONST VOID *Fdt, A210_PCIE *Pcie)
{
  CONST UINT32 *Data;
  INT32 Length, Index;
  UINTN Region = 1;
  Data = FdtGetProp (Fdt, Pcie->Node, "ranges", &Length);
  if (!Data || Length <= 0 || Length % 28)
    return EFI_UNSUPPORTED;
  for (Index = 0; Index < Length / 4; Index += 7)
  {
    PCI_ROOT_BRIDGE_APERTURE *Aperture;
    EFI_STATUS Status;
    UINT32 Flags = A210Cell (Data + Index), Type;
    UINT64 Pci = ((UINT64)A210Cell (Data + Index + 1) << 32)
                 | A210Cell (Data + Index + 2);
    UINT64 Cpu = ((UINT64)A210Cell (Data + Index + 3) << 32)
                 | A210Cell (Data + Index + 4);
    UINT64 Size = ((UINT64)A210Cell (Data + Index + 5) << 32)
                  | A210Cell (Data + Index + 6);
    if (!Size || Cpu > MAX_UINT64 - Size || Pci > MAX_UINT64 - Size
        || Region >= 8)
      return EFI_UNSUPPORTED;
    if ((Flags & 0x03000000) == 0x01000000)
    {
      EFI_GCD_IO_SPACE_DESCRIPTOR Descriptor;
      // Reject a firmware environment whose CPU HOB cannot cover this
      // translated I/O window, before the generic bridge registers it.
      if (EFI_ERROR (gDS->GetIoSpaceDescriptor (Cpu, &Descriptor))
          || EFI_ERROR (gDS->GetIoSpaceDescriptor (
              Cpu + Size - 1, &Descriptor)))
        return EFI_UNSUPPORTED;
      Aperture = &Pcie->Root.Io;
      Type = DW_IATU_IO;
    }
    else if ((Flags & 0x03000000) == 0x02000000
             || (Flags & 0x03000000) == 0x03000000)
    {
      Type = DW_IATU_MEM;
      if (Flags & BIT30)
        Aperture = Pci > MAX_UINT32 ? &Pcie->Root.PMemAbove4G
                                    : &Pcie->Root.PMem;
      else
        Aperture = Pci > MAX_UINT32 ? &Pcie->Root.MemAbove4G
                                    : &Pcie->Root.Mem;
    }
    else
      return EFI_UNSUPPORTED;
    if (Aperture->Base <= Aperture->Limit)
      return EFI_UNSUPPORTED;
    Aperture->Base = Pci;
    Aperture->Limit = Pci + Size - 1;
    Aperture->Translation = Pci - Cpu;
    Status = A210PcieAtu (Pcie, Region++, Type, Cpu, Pci, Size);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
A210PciInitialize (PCI_ROOT_BRIDGE **Bridges, UINTN *Count)
{
  CONST VOID *Fdt = A210GetFdt ();
  PCI_ROOT_BRIDGE *Result;
  INT32 Node = -1;
  UINTN Found = 0;
  EFI_STATUS Status;
  if (!Fdt || !Bridges || !Count)
    return EFI_INVALID_PARAMETER;
  *Count = 0;
  *Bridges = NULL;
  Status = A210GpioHogs (Fdt);
  if (EFI_ERROR (Status))
    return Status;
  Result = AllocateZeroPool (sizeof (PCI_ROOT_BRIDGE) * 2);
  if (!Result)
    return EFI_OUT_OF_RESOURCES;
  while ((Node = FdtNodeOffsetByCompatible (
      Fdt, Node, "zhihe,a210-pcie")) >= 0)
  {
    UINTN Base, Size, Index;
    A210_PCIE *Pcie;
    CONST UINT32 *Data;
    INT32 Length;
    if (!A210NodeEnabled (Fdt, Node))
      continue;
    if (EFI_ERROR (A210Reg (Fdt, Node, 0, &Base, &Size))
        || (Base != A210_PCIE_DBI0
            && Base != A210_PCIE_DBI0 + A210_PCIE_DBI_STRIDE)
        || Size < 0x400000)
      continue;
    Index = (Base - A210_PCIE_DBI0) / A210_PCIE_DBI_STRIDE;
    Pcie = &mPcie[Index];
    if (Pcie->Ready)
      continue;
    ZeroMem (Pcie, sizeof (*Pcie));
    Pcie->Dbi = Base;
    Pcie->Index = Index;
    Pcie->Node = Node;
    if (EFI_ERROR (A210Reg (Fdt, Node, 1, &Pcie->Config, &Size))
        || Size < A210_IATU_ALIGNMENT
        || Pcie->Config != (Index ? A210_PCIE1_WINDOW : A210_PCIE0_WINDOW)
        || EFI_ERROR (A210Reg (Fdt, Node, 2, &Pcie->App, &Size))
        || Pcie->App != A210_PCIE_APP0 + Index * A210_PCIE_APP_STRIDE)
      continue;
    Data = FdtGetProp (Fdt, Node, "num-lanes", &Length);
    if (!Data || Length != 4)
      continue;
    Pcie->Lanes = A210Cell (Data);
    if (Pcie->Lanes != 1 && Pcie->Lanes != 2 && Pcie->Lanes != 4)
      continue;
    Data = FdtGetProp (Fdt, Node, "max-link-speed", &Length);
    Pcie->Speed = Data && Length == 4 ? A210Cell (Data) : 3;
    if (Pcie->Speed < 1 || Pcie->Speed > 3)
      continue;
    Pcie->Root.Segment = Index;
    Pcie->Root.Supports = EFI_PCI_ATTRIBUTE_DUAL_ADDRESS_CYCLE;
    Pcie->Root.DmaAbove4G = TRUE;
    Pcie->Root.AllocationAttributes = EFI_PCI_HOST_BRIDGE_MEM64_DECODE;
    Pcie->Root.DevicePath = (VOID *)&mPath[Index];
    Pcie->Root.Io.Base = MAX_UINT64;
    Pcie->Root.Mem.Base = MAX_UINT64;
    Pcie->Root.PMem.Base = MAX_UINT64;
    Pcie->Root.MemAbove4G.Base = MAX_UINT64;
    Pcie->Root.PMemAbove4G.Base = MAX_UINT64;
    Data = FdtGetProp (Fdt, Node, "bus-range", &Length);
    if (!Data || Length != 8 || A210Cell (Data) != 0
        || A210Cell (Data + 1) > 255 || !A210Cell (Data + 1))
      continue;
    Pcie->Root.Bus.Limit = A210Cell (Data + 1);
    Status = A210PcieHardwareInit (Fdt, Pcie);
    if (!EFI_ERROR (Status))
      Status = Ranges (Fdt, Pcie);
    if (EFI_ERROR (Status))
    {
      DEBUG ((DEBUG_ERROR, "PCIe%u initialization: %r\n", Index, Status));
      continue;
    }
    Pcie->Ready = TRUE;
    Result[Found++] = Pcie->Root;
  }
  if (!Found)
  {
    FreePool (Result);
    return EFI_NOT_FOUND;
  }
  *Bridges = Result;
  *Count = Found;
  return EFI_SUCCESS;
}

STATIC UINTN
ConfigAddress (UINT64 Address)
{
  UINTN Segment = (Address >> 32) & 0xffff;
  UINT32 Bus = (Address >> 20) & 255;
  UINT32 Device = (Address >> 15) & 31;
  UINT32 Function = (Address >> 12) & 7;
  A210_PCIE *Pcie;
  if (Address & 0xffff0000f0000000ULL || Segment >= 2)
    return 0;
  Pcie = &mPcie[Segment];
  if (!Pcie->Ready || Bus > Pcie->Root.Bus.Limit)
    return 0;
  if (!Bus)
    return Device || Function ? 0 : Pcie->Dbi + (Address & 0xfff);
  if ((Bus == 1 && Device)
      || (MmioRead32 (Pcie->App + A210_PCIE_LINK_STATUS)
          & A210_PCIE_LINK_UP) != A210_PCIE_LINK_UP)
    return 0;
  if (EFI_ERROR (A210PcieAtu (Pcie, 0,
      Bus == 1 ? DW_IATU_CFG0 : DW_IATU_CFG1, Pcie->Config,
      ((UINT64)Bus << 24) | (Device << 19) | (Function << 16),
      A210_IATU_ALIGNMENT)))
    return 0;
  return Pcie->Config + (Address & 0xfff);
}

UINT32
A210PciRead (UINT64 Address, UINTN Width)
{
  EFI_TPL OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  UINTN Register = ConfigAddress (Address);
  UINT32 Value = MAX_UINT32;
  if (Register)
  {
    if (Width == 1)
      Value = MmioRead8 (Register);
    else if (Width == 2)
      Value = MmioRead16 (Register);
    else if (Width == 4)
      Value = MmioRead32 (Register);
  }
  gBS->RestoreTPL (OldTpl);
  return Value;
}

VOID
A210PciWrite (UINT64 Address, UINTN Width, UINT32 Value)
{
  EFI_TPL OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  UINTN Register = ConfigAddress (Address);
  if (Register)
  {
    if (Width == 1)
      MmioWrite8 (Register, Value);
    else if (Width == 2)
      MmioWrite16 (Register, Value);
    else if (Width == 4)
      MmioWrite32 (Register, Value);
  }
  gBS->RestoreTPL (OldTpl);
}
