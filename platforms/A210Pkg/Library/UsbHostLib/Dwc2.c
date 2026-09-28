// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/A210UsbHostLib.h>
#include <Library/Dwc2HostPlatformLib.h>

EFI_STATUS
Dwc2HostPlatformInit (UINTN *Base)
{
  return A210UsbHostInit (FALSE, Base);
}
