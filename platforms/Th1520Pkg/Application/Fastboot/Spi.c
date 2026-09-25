// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Fastboot.h"
#include <Library/Th1520SpiLib.h>
#include <Library/PrintLib.h>

EFI_STATUS
SpiFlash (CONST VOID *Buffer, UINTN Length)
{
  EFI_STATUS Status;
  EFI_TPL Previous;
  CHAR8 Progress[64];
  if (!Buffer || !Length || Length > SIZE_8MB - SIZE_256KB)
    return EFI_BAD_BUFFER_SIZE;
  // Serialize access with variable writes and other DXE callbacks.
  Previous = gBS->RaiseTPL (TPL_NOTIFY);
  Status = Th1520SpiInit ();
  for (UINTN Offset = 0; !EFI_ERROR (Status) && Offset < Length;
       Offset += SIZE_4KB)
  {
    if (!(Offset & 0xffff))
    {
      AsciiSPrint (Progress, sizeof (Progress),
                   "INFOWriting SPI 0x%08x", (UINT32)Offset);
      Status = UsbSend (Progress, AsciiStrLen (Progress));
      if (EFI_ERROR (Status))
        break;
    }
    Status = Th1520SpiErase (Offset, SIZE_4KB);
    if (!EFI_ERROR (Status))
      Status = Th1520SpiProgram (Offset, MIN (SIZE_4KB, Length - Offset),
                                 (CONST UINT8 *)Buffer + Offset);
  }
  gBS->RestoreTPL (Previous);
  return Status;
}
