// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210PciLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

PCI_ROOT_BRIDGE * EFIAPI
PciHostBridgeGetRootBridges (UINTN *Count)
{
  PCI_ROOT_BRIDGE *Bridges = NULL;
  EFI_STATUS Status = A210PciInitialize (&Bridges, Count);
  if (EFI_ERROR (Status))
    DEBUG ((DEBUG_ERROR, "A210 PCI host bridges: %r\n", Status));
  return Bridges;
}

VOID EFIAPI
PciHostBridgeFreeRootBridges (PCI_ROOT_BRIDGE *Bridges, UINTN Count)
{
  if (Bridges)
    FreePool (Bridges);
}

VOID EFIAPI
PciHostBridgeResourceConflict (EFI_HANDLE HostBridgeHandle,
                               VOID *Configuration)
{
  DEBUG ((DEBUG_ERROR, "A210 PCI resource allocation conflict\n"));
}
