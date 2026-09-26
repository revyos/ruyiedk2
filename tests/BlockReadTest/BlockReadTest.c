// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define READ_BLOCKS 128
#define SPEED_BYTES SIZE_4MB

STATIC EFI_STATUS
CheckRange (EFI_BLOCK_IO_PROTOCOL *Io, EFI_LBA Start, VOID *Multi,
            VOID *Single)
{
  EFI_STATUS Status;
  UINTN BlockSize = Io->Media->BlockSize;
  UINTN Bytes = READ_BLOCKS * BlockSize;

  Status = Io->ReadBlocks (Io, Io->Media->MediaId, Start, Bytes, Multi);
  if (EFI_ERROR (Status))
    return Status;
  for (UINTN I = 0; I < READ_BLOCKS; I++)
  {
    Status = Io->ReadBlocks (Io, Io->Media->MediaId, Start + I, BlockSize,
                             (UINT8 *)Single + I * BlockSize);
    if (EFI_ERROR (Status))
      return Status;
  }
  return CompareMem (Multi, Single, Bytes) ? EFI_CRC_ERROR : EFI_SUCCESS;
}

EFI_STATUS EFIAPI
BlockReadTest (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE *Handles;
  EFI_BLOCK_IO_PROTOCOL *Io;
  EFI_STATUS Status, Result = EFI_SUCCESS;
  UINTN Count, Tested = 0;
  CHAR8 Text[192];
  VOID *Multi, *Single;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status))
    return Status;
  Multi = AllocatePages (EFI_SIZE_TO_PAGES (SPEED_BYTES));
  Single = AllocatePages (EFI_SIZE_TO_PAGES (SPEED_BYTES));
  if (Multi == NULL || Single == NULL)
  {
    Result = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  for (UINTN I = 0; I < Count; I++)
  {
    Status = gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid,
                                  (VOID **)&Io);
    if (EFI_ERROR (Status) || !Io->Media->MediaPresent
        || Io->Media->LogicalPartition)
      continue;
    if (Io->Media->BlockSize == 0 || Io->Media->BlockSize > EFI_PAGE_SIZE
        || Io->Media->IoAlign > EFI_PAGE_SIZE
        || Io->Media->LastBlock < READ_BLOCKS)
      continue;
    Tested++;
    // Compare the multi-block path with independent single-block reads,
    // including an unaligned start and the end of each physical area.
    Status = CheckRange (Io, 0, Multi, Single);
    if (!EFI_ERROR (Status))
      Status = CheckRange (Io, 1, Multi, Single);
    if (!EFI_ERROR (Status))
      Status = CheckRange (Io, Io->Media->LastBlock - READ_BLOCKS + 1,
                           Multi, Single);
    UINTN Size
        = AsciiSPrint (Text, sizeof (Text),
                       "BlockReadTest: media %u, %Lu blocks x %u: %r\r\n",
                       Io->Media->MediaId, Io->Media->LastBlock + 1,
                       Io->Media->BlockSize, Status);
    SerialPortWrite ((UINT8 *)Text, Size);
    if (EFI_ERROR (Status))
    {
      Result = Status;
      continue;
    }
    if ((Io->Media->LastBlock + 1) >= SPEED_BYTES / Io->Media->BlockSize)
    {
      UINT64 Start = GetPerformanceCounter ();
      Status
          = Io->ReadBlocks (Io, Io->Media->MediaId, 0, SPEED_BYTES, Multi);
      UINT64 Elapsed
          = GetTimeInNanoSecond (GetPerformanceCounter () - Start)
            / 1000000;
      Size = AsciiSPrint (Text, sizeof (Text),
                          "BlockReadTest: 4 MiB in %Lu ms: %r\r\n",
                          Elapsed, Status);
      SerialPortWrite ((UINT8 *)Text, Size);
      if (EFI_ERROR (Status))
        Result = Status;
    }
  }
  if (Tested == 0)
    Result = EFI_NOT_FOUND;
  UINTN Size
      = AsciiSPrint (Text, sizeof (Text),
                     "BlockReadTest: %u areas, %r\r\n", Tested, Result);
  SerialPortWrite ((UINT8 *)Text, Size);
Done:
  if (Multi != NULL)
    FreePages (Multi, EFI_SIZE_TO_PAGES (SPEED_BYTES));
  if (Single != NULL)
    FreePages (Single, EFI_SIZE_TO_PAGES (SPEED_BYTES));
  FreePool (Handles);
  return Result;
}
