// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Fastboot.h"
#include <Library/Th1520SpiLib.h>
#include <Library/Th1520ByteStorageLib.h>
#include <Library/PrintLib.h>

STATIC EFI_STATUS
Transfer (UINT64 Offset, UINTN Length, VOID *Buffer, BOOLEAN Write)
{
  EFI_STATUS Status;
  CHAR8 Progress[64];
  if (!Write) return Th1520SpiRead ((UINTN)Offset, Length, Buffer);
  if (!(Offset & 0xffff))
  {
    AsciiSPrint (Progress, sizeof (Progress), "INFOWriting SPI 0x%08x", (UINT32)Offset);
    Status = UsbSend (Progress, AsciiStrLen (Progress));
    if (EFI_ERROR (Status)) return Status;
  }
  for (UINTN Done = 0; Done < Length; Done += SIZE_4KB)
  {
    Status = Th1520SpiErase ((UINTN)Offset + Done, SIZE_4KB);
    if (!EFI_ERROR (Status))
      Status = Th1520SpiProgram ((UINTN)Offset + Done, SIZE_4KB,
                                (CONST UINT8 *)Buffer + Done);
    if (EFI_ERROR (Status)) return Status;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Access (UINT64 Offset, UINTN Length, VOID *Buffer, BOOLEAN Write)
{
  EFI_STATUS Status;
  EFI_TPL Previous = gBS->RaiseTPL (TPL_NOTIFY);
  Status = Th1520SpiInit ();
  if (!EFI_ERROR (Status))
    Status = Th1520StorageBytes (SIZE_8MB, SIZE_4KB, Transfer,
                                Offset, Length, Buffer, Write);
  gBS->RestoreTPL (Previous);
  return Status;
}

EFI_STATUS
SpiFlash (UINT64 Offset, CONST VOID *Buffer, UINTN Length)
{
  return Access (Offset, Length, (VOID *)Buffer, TRUE);
}

EFI_STATUS
SpiRead (UINT64 Offset, VOID *Buffer, UINTN Length)
{
  return Access (Offset, Length, Buffer, FALSE);
}
