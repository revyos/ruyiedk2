// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef A210_PCI_LIB_H_
#define A210_PCI_LIB_H_
#include <Uefi.h>
#include <Library/PciHostBridgeLib.h>

EFI_STATUS A210PciInitialize (PCI_ROOT_BRIDGE **Bridges, UINTN *Count);
UINT32 A210PciRead (UINT64 Address, UINTN Width);
VOID A210PciWrite (UINT64 Address, UINTN Width, UINT32 Value);
#endif
