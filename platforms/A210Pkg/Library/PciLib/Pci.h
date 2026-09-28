// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef A210_PCI_INTERNAL_H_
#define A210_PCI_INTERNAL_H_
#include <Uefi.h>
#include <A210.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210PciLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define A210_PCIE_CRG 0x0a000000
#define A210_PCIE_DBI0 0x0b000000
#define A210_PCIE_DBI_STRIDE 0x800000
#define A210_PCIE_APP0 0x0a004000
#define A210_PCIE_APP_STRIDE 0x1000
#define A210_PCIE_CONTROL 0x00
#define A210_PCIE_ROOT_PORT (BIT4 | 4U)
#define A210_PCIE_LTSSM BIT8
#define A210_PCIE_LINK_STATUS 0x34
#define A210_PCIE_LINK_UP (BIT0 | BIT4)
#define DW_DBI2_OFFSET 0x100000
#define DW_IATU_OFFSET 0x300000
#define DW_IATU_STRIDE 0x200
#define A210_IATU_ALIGNMENT 0x10000
#define DW_IATU_CONTROL1 0x00
#define DW_IATU_CONTROL2 0x04
#define DW_IATU_BASE_LO 0x08
#define DW_IATU_BASE_HI 0x0c
#define DW_IATU_LIMIT 0x10
#define DW_IATU_TARGET_LO 0x14
#define DW_IATU_TARGET_HI 0x18
#define DW_IATU_ENABLE BIT31
#define DW_IATU_CFG0 4U
#define DW_IATU_CFG1 5U
#define DW_IATU_MEM 0U
#define DW_IATU_IO 2U
#define DW_PORT_LINK 0x710
#define DW_LINK_WIDTH_SPEED 0x80c
#define DW_SPEED_CHANGE BIT17
#define DW_DBI_WRITE_ENABLE 0x8bc
#define PCI_CAP_POINTER 0x34
#define PCI_CAP_EXPRESS 0x10
#define PCIE_LINK_CAP 0x0c
#define PCIE_LINK_CONTROL 0x10
#define PCIE_LINK_STATUS 0x12
#define PCIE_LINK_CONTROL2 0x30

typedef struct {
  UINTN Dbi;
  UINTN App;
  UINTN Config;
  UINT32 Index;
  UINT32 Lanes;
  UINT32 Speed;
  INT32 Node;
  UINT32 ResetGpio[3];
  BOOLEAN Ready;
  PCI_ROOT_BRIDGE Root;
} A210_PCIE;

EFI_STATUS A210PciePhyInit (CONST VOID *Fdt, INT32 Node);
EFI_STATUS A210PcieHardwareInit (CONST VOID *Fdt, A210_PCIE *Pcie);
EFI_STATUS A210PcieAtu (A210_PCIE *Pcie, UINTN Region, UINT32 Type,
                       UINT64 Cpu, UINT64 Pci, UINT64 Size);
#endif
