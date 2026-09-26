// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/DevicePathLib.h>
#include <Library/DisplayConsoleLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC VOID *GraphicsRegistration;
STATIC BOOLEAN ConsoleReady;

STATIC VOID EFIAPI
GraphicsAvailable (EFI_EVENT Event, VOID *Context)
{
  EFI_HANDLE Handle;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINT32 Width, Height;
  UINTN Size;
  while (TRUE)
  {
    Size = sizeof (Handle);
    if (EFI_ERROR (gBS->LocateHandle (
            ByRegisterNotify, NULL, GraphicsRegistration, &Size, &Handle)))
      break;
    Path = DevicePathFromHandle (Handle);
    if (Path == NULL)
      continue;
    if (!EFI_ERROR (gBS->HandleProtocol (
            Handle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop))
        && Gop->Mode != NULL && Gop->Mode->Info != NULL)
    {
      // Preserve the mode selected by GOP from the sink's EDID, including
      // in setup. GraphicsConsole otherwise chooses its own resolution.
      Width = Gop->Mode->Info->HorizontalResolution;
      Height = Gop->Mode->Info->VerticalResolution;
      PcdSet32S (PcdVideoHorizontalResolution, Width);
      PcdSet32S (PcdVideoVerticalResolution, Height);
      PcdSet32S (PcdSetupVideoHorizontalResolution, Width);
      PcdSet32S (PcdSetupVideoVerticalResolution, Height);
    }
    EfiBootManagerUpdateConsoleVariable (ConOut, Path, NULL);
    EfiBootManagerUpdateConsoleVariable (ErrOut, Path, NULL);
    if (ConsoleReady)
      EfiBootManagerConnectAllDefaultConsoles ();
  }
}

VOID
DisplayConsoleInitialize (VOID)
{
  EfiCreateProtocolNotifyEvent (&gEfiGraphicsOutputProtocolGuid,
                                TPL_CALLBACK, GraphicsAvailable, NULL,
                                &GraphicsRegistration);
}
VOID
DisplayConsoleReady (VOID)
{
  ConsoleReady = TRUE;
}
