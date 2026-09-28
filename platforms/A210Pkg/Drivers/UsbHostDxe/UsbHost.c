// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/A210UsbHostLib.h>
#include <Library/DebugLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC EFI_EVENT mPollEvent;

STATIC VOID EFIAPI
PollTypec (EFI_EVENT Event, VOID *Context)
{
  EFI_STATUS Status = A210UsbHostPoll ();
  if (EFI_ERROR (Status))
  {
    DEBUG ((DEBUG_ERROR, "A210 Type-C polling stopped: %r\n", Status));
    A210UsbHostStop ();
    return;
  }
  // Slow I2C must not continuously requeue a periodic notification.
  gBS->SetTimer (Event, TimerRelative, 100000);
}

STATIC VOID EFIAPI
ExitBootServices (EFI_EVENT Event, VOID *Context)
{
  // Leave the active connection intact for the operating system.
  gBS->CloseEvent (mPollEvent);
  mPollEvent = NULL;
}

EFI_STATUS EFIAPI
UsbHostInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE Handle = NULL;
  EFI_EVENT ExitEvent = NULL;
  UINTN Base;
  EFI_STATUS Status = A210UsbHostInit (TRUE, &Base);
  if (EFI_ERROR (Status))
  {
    if (Status != EFI_NOT_FOUND)
      DEBUG ((DEBUG_ERROR, "A210 USB3 initialization: %r\n", Status));
    return Status;
  }
  Status = A210UsbHostPoll ();
  if (Status != EFI_NOT_FOUND)
  {
    if (EFI_ERROR (Status))
      goto Error;
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                               PollTypec, NULL, &mPollEvent);
    if (EFI_ERROR (Status))
      goto Error;
    Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY,
        ExitBootServices, NULL, &gEfiEventExitBootServicesGuid,
        &ExitEvent);
    if (EFI_ERROR (Status))
      goto Error;
    Status = gBS->SetTimer (mPollEvent, TimerRelative, 100000);
    if (EFI_ERROR (Status))
      goto Error;
  }
  Status = RegisterNonDiscoverableMmioDevice (
      NonDiscoverableDeviceTypeXhci,
      NonDiscoverableDeviceDmaTypeNonCoherent,
      NULL, &Handle, 1, Base, (UINTN)0x10000);
  if (!EFI_ERROR (Status))
    return Status;
Error:
  if (ExitEvent)
    gBS->CloseEvent (ExitEvent);
  if (mPollEvent)
    gBS->CloseEvent (mPollEvent);
  A210UsbHostStop ();
  return Status;
}
