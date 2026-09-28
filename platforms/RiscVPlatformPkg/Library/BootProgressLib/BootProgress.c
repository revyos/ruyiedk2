// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BootLogoLib.h>
#include <Library/BootProgressLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT16 mBootTimeout;
STATIC UINTN mProgress;
STATIC BOOLEAN mWaiting, mGraphicsProgress;

STATIC VOID EFIAPI
FinishStartup (EFI_EVENT Event, VOID *Context)
{
  if (mWaiting)
  {
    SerialPortWrite ((UINT8 *)"\r\n", 2);
    mWaiting = FALSE;
    // Include USB devices which appeared during the native BDS hotkey wait.
    EfiBootManagerRefreshAllBootOption ();
  }
  BootLogoDisableLogo ();
}

VOID EFIAPI
BootProgressStart (VOID)
{
  EFI_EVENT Event;
  STATIC CONST CHAR8 Keys[] =
      "Enter: Boot    F2: Setup    F9: USB Fastboot    F12: Boot Menu\r\n";
  mBootTimeout = PcdGet16 (PcdPlatformBootTimeOut);
  mProgress = 0;
  mWaiting = mBootTimeout != 0;
  mGraphicsProgress = !EFI_ERROR (BootLogoEnableLogo ());
  SerialPortWrite ((UINT8 *)Keys, sizeof (Keys) - 1);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, FinishStartup, NULL,
                      &gEfiEventReadyToBootGuid, &Event);
  // BDS owns the timeout and dispatches the registered hotkeys.
}

VOID EFIAPI
BootProgressUpdate (UINT16 Remaining)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White = {0xff, 0xff, 0xff, 0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = {0, 0, 0, 0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Blue = {0xdd, 0x63, 0x3e, 0};
  CHAR16 Title[80];
  CHAR8 Serial[64];
  UINTN Progress, Size, SecondsWidth = 1;
  if (mBootTimeout == 0)
  {
    return;
  }
  Progress = (mBootTimeout - MIN (Remaining, mBootTimeout)) * 100U
             / mBootTimeout;
  // BootLogoUpdateProgress centers each title without erasing the old one.
  // Keep its width fixed when the countdown loses a decimal digit.
  for (UINTN Timeout = mBootTimeout; Timeout >= 10; Timeout /= 10)
    SecondsWidth++;
  UnicodeSPrint (Title, sizeof (Title),
      L"Enter: Boot  F2: Setup  F9: Fastboot  F12: Boot Menu  (%*us)",
      SecondsWidth, Remaining);
  if (mGraphicsProgress)
  {
    BootLogoUpdateProgress (White, Black, Title, Blue, Progress, mProgress);
  }
  mProgress = Progress;
  Size = AsciiSPrint (Serial, sizeof (Serial),
                     "\rBooting in %2u seconds...", Remaining);
  SerialPortWrite ((UINT8 *)Serial, Size);
  if (Remaining == 0)
  {
    FinishStartup (NULL, NULL);
  }
}
