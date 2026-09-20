// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
EFI_STATUS UsbDmaInitialize (VOID);
VOID UsbDmaRelease (VOID);
EFI_STATUS
UsbDmaBind (EFI_PCI_IO_PROTOCOL *Pci);
VOID UsbDmaSync (VOID);
BOOLEAN UsbDmaIdle (VOID);
