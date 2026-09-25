// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Targets.h"
#include <Library/BaseLib.h>

TH1520_FLASH_TARGET
Th1520RecoveryTarget (CONST CHAR8 *Name)
{
  if (Name != NULL)
  {
    if (AsciiStrCmp (Name, "spi") == 0)
    {
      return Th1520TargetSpi;
    }
    if (AsciiStrCmp (Name, "emmcboot0") == 0)
    {
      return Th1520TargetEmmcBoot0;
    }
    if (AsciiStrCmp (Name, "emmcboot1") == 0)
    {
      return Th1520TargetEmmcBoot1;
    }
  }
  return Th1520TargetNone;
}

UINTN
Th1520TargetMaximum (TH1520_FLASH_TARGET Target, UINT64 BootAreaSize)
{
  if (Target == Th1520TargetSpi)
  {
    return SIZE_8MB - SIZE_256KB;
  }
  if ((Target == Th1520TargetEmmcBoot0 || Target == Th1520TargetEmmcBoot1) &&
      BootAreaSize <= MAX_UINTN)
  {
    return Target == Th1520TargetEmmcBoot0 ?
        (BootAreaSize > SIZE_8KB ? BootAreaSize - SIZE_8KB : 0) :
        (UINTN)BootAreaSize;
  }
  return 0;
}

BOOLEAN
Th1520ValidRawImage (CONST VOID *Buffer, UINTN Length, UINTN Maximum)
{
  if (Buffer == NULL || Length == 0 || Length > Maximum)
  {
    return FALSE;
  }
  // Sparse containers must never be written verbatim to a firmware device.
  return Length < sizeof (UINT32) || ReadUnaligned32 (Buffer) != 0xed26ff3aU;
}
