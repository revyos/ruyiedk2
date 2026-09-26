// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/A210BootMediaLib.h>
#include <Library/FastbootPlatformLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define IO_BYTES SIZE_64KB
STATIC EFI_BLOCK_IO_PROTOCOL *mBoot[2];

STATIC INTN
Target (CONST CHAR8 *Name)
{
  if (!AsciiStrCmp (Name, "emmc0boot0"))
    return 0;
  if (!AsciiStrCmp (Name, "emmc0boot1"))
    return 1;
  return -1;
}

STATIC UINT64
Capacity (EFI_BLOCK_IO_PROTOCOL *Io)
{
  return (Io->Media->LastBlock + 1) * Io->Media->BlockSize;
}

EFI_STATUS
FastbootPlatformInit (VOID)
{
  return A210LocateBootAreas (mBoot);
}

VOID
FastbootPlatformStop (VOID)
{
  ZeroMem (mBoot, sizeof (mBoot));
}

EFI_STATUS
FastbootGetVar (CONST CHAR8 *Name, CHAR8 *Value, UINTN Size)
{
  CONST CHAR8 *Partition;
  INTN Index;

  if (!AsciiStrCmp (Name, "product"))
    return UnicodeStrToAsciiStrS (PcdGetPtr (PcdFirmwareVersionString),
                                  Value, Size);
  if (!AsciiStrCmp (Name, "serialno"))
    return AsciiStrCpyS (Value, Size, "a210");
  if (!AsciiStrnCmp (Name, "partition-size:", 15))
    Partition = Name + 15;
  else if (!AsciiStrnCmp (Name, "partition-type:", 15))
    Partition = Name + 15;
  else if (!AsciiStrnCmp (Name, "has-slot:", 9))
    Partition = Name + 9;
  else
    return EFI_NOT_FOUND;
  Index = Target (Partition);
  if (Index < 0 || mBoot[Index] == NULL)
    return EFI_NOT_FOUND;
  if (!AsciiStrnCmp (Name, "partition-size:", 15))
    AsciiSPrint (Value, Size, "0x%Lx", Capacity (mBoot[Index]));
  else
    AsciiStrCpyS (Value, Size,
                  !AsciiStrnCmp (Name, "has-slot:", 9) ? "no" : "raw");
  return EFI_SUCCESS;
}

EFI_STATUS
FastbootRead (CONST CHAR8 *Name, UINT64 Offset, UINTN Size, VOID *Data)
{
  INTN Index = Target (Name);
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_BLOCK_IO_PROTOCOL *Io;
  UINT8 *Bounce;
  UINTN BlockSize;

  if (Index < 0 || mBoot[Index] == NULL)
    return EFI_NOT_FOUND;
  Io = mBoot[Index];
  if (Offset > Capacity (Io) || Size > Capacity (Io) - Offset)
    return EFI_BAD_BUFFER_SIZE;
  Bounce = AllocatePages (EFI_SIZE_TO_PAGES (IO_BYTES));
  if (Bounce == NULL)
    return EFI_OUT_OF_RESOURCES;
  BlockSize = Io->Media->BlockSize;
  while (Size)
  {
    UINTN Skip = Offset % BlockSize;
    UINTN Bytes = MIN (Size, IO_BYTES - Skip);
    UINTN ReadSize = ALIGN_VALUE (Skip + Bytes, BlockSize);
    Status = Io->ReadBlocks (Io, Io->Media->MediaId, Offset / BlockSize,
                             ReadSize, Bounce);
    if (EFI_ERROR (Status))
      break;
    CopyMem (Data, Bounce + Skip, Bytes);
    Data = (UINT8 *)Data + Bytes;
    Offset += Bytes;
    Size -= Bytes;
  }
  FreePages (Bounce, EFI_SIZE_TO_PAGES (IO_BYTES));
  return Status;
}

EFI_STATUS
FastbootFlash (CONST CHAR8 *Name, CONST VOID *Data, UINTN Size)
{
  INTN Index = Target (Name);
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_BLOCK_IO_PROTOCOL *Io;
  UINT8 *Bounce;
  UINTN BlockSize, Padded;

  if (Index < 0 || mBoot[Index] == NULL)
    return EFI_NOT_FOUND;
  Io = mBoot[Index];
  if (Io->Media->ReadOnly)
    return EFI_WRITE_PROTECTED;
  BlockSize = Io->Media->BlockSize;
  if (!Size || Size > Capacity (Io))
    return EFI_BAD_BUFFER_SIZE;
  Padded = ALIGN_VALUE (Size, BlockSize);
  if (Padded > Capacity (Io))
    return EFI_BAD_BUFFER_SIZE;
  Bounce = AllocatePages (EFI_SIZE_TO_PAGES (IO_BYTES));
  if (Bounce == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN Offset = 0; Offset < Padded;)
  {
    UINTN Bytes = MIN (IO_BYTES, Padded - Offset);
    UINTN Payload = MIN (Bytes, Size - Offset);
    SetMem (Bounce, Bytes, 0xff);
    CopyMem (Bounce, (CONST UINT8 *)Data + Offset, Payload);
    Status = Io->WriteBlocks (Io, Io->Media->MediaId, Offset / BlockSize,
                              Bytes, Bounce);
    if (EFI_ERROR (Status))
      goto Out;
    Offset += Bytes;
  }
  Status = Io->FlushBlocks (Io);
  if (EFI_ERROR (Status))
    goto Out;
  for (UINTN Offset = 0; Offset < Padded;)
  {
    UINTN Bytes = MIN (IO_BYTES, Padded - Offset);
    UINTN Payload = MIN (Bytes, Size - Offset);
    Status = Io->ReadBlocks (Io, Io->Media->MediaId, Offset / BlockSize,
                             Bytes, Bounce);
    if (EFI_ERROR (Status))
      goto Out;
    if (CompareMem (Bounce, (CONST UINT8 *)Data + Offset, Payload))
    {
      Status = EFI_CRC_ERROR;
      goto Out;
    }
    for (UINTN I = Payload; I < Bytes; I++)
      if (Bounce[I] != 0xff)
      {
        Status = EFI_CRC_ERROR;
        goto Out;
      }
    Offset += Bytes;
  }
Out:
  FreePages (Bounce, EFI_SIZE_TO_PAGES (IO_BYTES));
  return Status;
}
