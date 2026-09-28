// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210UsbHostLib.h>
#include <Library/DebugLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>

EFI_STATUS EFIAPI
UsbHostInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE Handle = NULL;
  UINTN Base;
  EFI_STATUS Status = A210UsbHostInit (TRUE, &Base);
  if (EFI_ERROR (Status))
  {
    if (Status != EFI_NOT_FOUND)
      DEBUG ((DEBUG_ERROR, "A210 USB3 initialization: %r\n", Status));
    return Status;
  }
  return RegisterNonDiscoverableMmioDevice (
      NonDiscoverableDeviceTypeXhci,
      NonDiscoverableDeviceDmaTypeNonCoherent,
      NULL, &Handle, 1, Base, (UINTN)0x10000);
}
