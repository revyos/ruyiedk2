// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseRiscVSbiLib.h>
#include <Library/ResetSystemLib.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static jmp_buf Stop;
static BOOLEAN Supported, Called;
static UINTN Expected;
EFI_STATUS EFIAPI
SbiSystemReset (UINTN Type, UINTN Reason)
{
  assert (Type == Expected && Reason == SBI_SRST_RESET_REASON_NONE);
  Called = TRUE;
  if (Supported)
    longjmp (Stop, 1);
  return EFI_UNSUPPORTED;
}
VOID EFIAPI
CpuDeadLoop (VOID)
{
  assert (Called && !Supported);
  longjmp (Stop, 2);
}
int main (void)
{
  for (unsigned Support = 0; Support < 2; Support++)
    for (unsigned Type = EfiResetCold; Type <= EfiResetPlatformSpecific;
         Type++)
    {
      Supported = Support;
      Called = FALSE;
      Expected = Type == EfiResetShutdown ? SBI_SRST_RESET_TYPE_SHUTDOWN :
          Type == EfiResetWarm ? SBI_SRST_RESET_TYPE_WARM_REBOOT :
          SBI_SRST_RESET_TYPE_COLD_REBOOT;
      int Result = setjmp (Stop);
      if (!Result)
      {
        ResetSystem (Type, EFI_SUCCESS, 0, NULL);
        assert (0);
      }
      assert (Called && Result == (Supported ? 1 : 2));
    }
  puts ("EFI reset: cold/warm/shutdown via SRST; failure never returns");
}
