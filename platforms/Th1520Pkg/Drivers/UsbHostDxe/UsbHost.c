// SPDX-License-Identifier: BSD-2-Clause-Patent
// TH1520 DWC3 host with board-specific USB routing and power control.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include "Dma.h"
#include <Library/IoLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Th1520UsbRole.h>
#include <Protocol/PciIo.h>

#define USB 0xffe7040000ULL
STATIC EFI_HANDLE Host;
STATIC BOOLEAN DeviceMode;
STATIC TH1520_USB_ROLE_PROTOCOL Role;
STATIC EFI_PCI_IO_PROTOCOL_IO_MEM SavedRead, SavedWrite;

STATIC EFI_STATUS
StartHost (VOID)
{
  UINT32 Value;
  EFI_STATUS Status = Th1520BoardUsbRole (FALSE);
  if (EFI_ERROR (Status))
    return Status;
  MmioOr32 (0xfffc02d104ULL, 15);
  MmioWrite32 (0xfffc02e0c0ULL, MAX_UINT32);
  MmioOr32 (0xffec03f034ULL, 1);
  MicroSecondDelay (10);
  MmioAnd32 (0xffec02c014ULL, ~7U);
  MicroSecondDelay (1000);
  MmioOr32 (0xffec02c014ULL, 7);
  MmioWrite32 (0xffec03f030ULL, 0x77f);
  MmioWrite32 (0xffec03f02cULL, 0x15150f0);
  if ((MmioRead32 (USB + 0xc120) & 0xffff0000U) != 0x55330000U)
  {
    return EFI_UNSUPPORTED;
  }
  MmioOr32 (USB + 0xc110, BIT11);
  MmioOr32 (USB + 0xc200, BIT31);
  MmioOr32 (USB + 0xc2c0, BIT31);
  MicroSecondDelay (100000);
  MmioAnd32 (USB + 0xc200, ~(BIT31 | BIT6 | BIT8));
  MmioAnd32 (USB + 0xc2c0, ~(BIT31 | BIT17));
  MicroSecondDelay (100000);
  Value = MmioRead32 (USB + 0xc110);
  Value &= ~((3U << 12) | (3U << 4) | BIT11 | BIT3 | BIT1 | BIT0);
  MmioWrite32 (USB + 0xc110, Value | (1U << 12));
  DEBUG ((DEBUG_INFO, "Meles USB host: HCCPARAMS1=%08x\n", MmioRead32 (USB + 0x10)));
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
ReadRegisters (EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT8 Bar, UINT64 Offset,
               UINTN Count, VOID *Buffer)
{
  EFI_STATUS Status;
  MemoryFence ();
  UsbDmaSync ();
  Status = SavedRead (This, Width, Bar, Offset, Count, Buffer);
  MemoryFence ();
  return Status;
}
STATIC EFI_STATUS EFIAPI
WriteRegisters (EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT8 Bar,
                UINT64 Offset, UINTN Count, VOID *Buffer)
{
  EFI_STATUS Status;
  // Publish descriptor stores before ringing a doorbell on weakly ordered C910.
  MemoryFence ();
  UsbDmaSync ();
  Status = SavedWrite (This, Width, Bar, Offset, Count, Buffer);
  MemoryFence ();
  return Status;
}
STATIC EFI_STATUS EFIAPI
InitializeDma (NON_DISCOVERABLE_DEVICE *Device)
{
  EFI_PCI_IO_PROTOCOL *Pci;
  EFI_STATUS Status = gBS->HandleProtocol (Host, &gEfiPciIoProtocolGuid, (VOID **)&Pci);
  if (!EFI_ERROR (Status))
  {
    // xHCI allocates descriptors in whole 64-byte units. Keep them in a
    // private pool and synchronize around register accesses, including
    // doorbells and the event-dequeue reads after completion. Streaming
    // data uses separate page-aligned bounce buffers.
    SavedRead = Pci->Mem.Read;
    SavedWrite = Pci->Mem.Write;
    Pci->Mem.Read = ReadRegisters;
    Pci->Mem.Write = WriteRegisters;
    Status = UsbDmaBind (Pci);
  }
  return Status;
}
STATIC EFI_STATUS EFIAPI
EnterDevice (TH1520_USB_ROLE_PROTOCOL *This)
{
  EFI_STATUS Status;
  if (This != &Role)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (DeviceMode)
  {
    return EFI_ALREADY_STARTED;
  }
  Status = gBS->DisconnectController (Host, NULL, NULL);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (!UsbDmaIdle ())
  {
    return EFI_DEVICE_ERROR;
  }
  DeviceMode = TRUE;
  return Th1520BoardUsbRole (TRUE);
}
STATIC EFI_STATUS EFIAPI
LeaveDevice (TH1520_USB_ROLE_PROTOCOL *This)
{
  EFI_STATUS Status;
  if (This != &Role)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!DeviceMode)
  {
    return EFI_NOT_STARTED;
  }
  Status = StartHost ();
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  DeviceMode = FALSE;
  return gBS->ConnectController (Host, NULL, NULL, TRUE);
}
STATIC TH1520_USB_ROLE_PROTOCOL Role = {EnterDevice, LeaveDevice};
EFI_STATUS EFIAPI
UsbHostInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  Status = UsbDmaInitialize ();
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = StartHost ();
  if (EFI_ERROR (Status))
  {
    UsbDmaRelease ();
    Th1520BoardUsbRole (TRUE);
    return Status;
  }
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gTh1520UsbRoleProtocolGuid,
                                          EFI_NATIVE_INTERFACE, &Role);
  if (EFI_ERROR (Status))
  {
    UsbDmaRelease ();
    Th1520BoardUsbRole (TRUE);
    return Status;
  }
  Status = RegisterNonDiscoverableMmioDevice (NonDiscoverableDeviceTypeXhci,
                                              NonDiscoverableDeviceDmaTypeNonCoherent,
                                              InitializeDma, &Host, 1, (UINTN)USB, (UINTN)0x10000);
  if (EFI_ERROR (Status))
  {
    gBS->UninstallProtocolInterface (ImageHandle, &gTh1520UsbRoleProtocolGuid, &Role);
    UsbDmaRelease ();
    Th1520BoardUsbRole (TRUE);
  }
  return Status;
}
