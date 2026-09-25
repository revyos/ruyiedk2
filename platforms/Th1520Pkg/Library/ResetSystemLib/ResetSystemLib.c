// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseRiscVSbiLib.h>

STATIC VOID
SystemReset (UINTN Type)
{
  // OpenSBI owns AON and the watchdog fallback, including RAM recovery.
  SbiSystemReset (Type, SBI_SRST_RESET_REASON_NONE);
  // ResetSystem must not return, even when firmware rejects the request.
  CpuDeadLoop ();
}

VOID EFIAPI
ResetCold (VOID)
{
  SystemReset (SBI_SRST_RESET_TYPE_COLD_REBOOT);
}

VOID EFIAPI
ResetWarm (VOID)
{
  SystemReset (SBI_SRST_RESET_TYPE_WARM_REBOOT);
}

VOID EFIAPI
ResetShutdown (VOID)
{
  SystemReset (SBI_SRST_RESET_TYPE_SHUTDOWN);
}

VOID EFIAPI
ResetPlatformSpecific (UINTN DataSize, VOID *ResetData)
{
  ResetCold ();
}

VOID EFIAPI
ResetSystem (EFI_RESET_TYPE Type, EFI_STATUS Status, UINTN Size,
             VOID *Data)
{
  switch (Type)
  {
    case EfiResetWarm:
      ResetWarm ();
      break;
    case EfiResetShutdown:
      ResetShutdown ();
      break;
    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (Size, Data);
      break;
    default:
      ResetCold ();
      break;
  }
}
