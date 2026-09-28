// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/A210BootMediaLib.h>
#include <Library/FastbootPlatformLib.h>
#include <Library/FastbootStorageLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define IO_BYTES SIZE_64KB
STATIC EFI_BLOCK_IO_PROTOCOL *mEmmc[3];
STATIC FASTBOOT_NVME_TARGET *mNvme;
STATIC UINTN mNvmeCount;

STATIC EFI_BLOCK_IO_PROTOCOL *
Target (CONST CHAR8 *Name)
{
  if (!AsciiStrCmp (Name, "emmc0"))
    return mEmmc[0];
  if (!AsciiStrCmp (Name, "emmc0boot0"))
    return mEmmc[1];
  if (!AsciiStrCmp (Name, "emmc0boot1"))
    return mEmmc[2];
  for (UINTN I = 0; I < mNvmeCount; I++)
    if (!AsciiStrCmp (Name, mNvme[I].Name))
      return mNvme[I].Io;
  return NULL;
}

STATIC UINT64
Capacity (EFI_BLOCK_IO_PROTOCOL *Io)
{
  return (Io->Media->LastBlock + 1) * Io->Media->BlockSize;
}

EFI_STATUS
FastbootPlatformInit (VOID)
{
  EFI_STATUS Status = A210LocateEmmcAreas (mEmmc);
  if (EFI_ERROR (Status))
    return Status;
  Status = FastbootLocateNvme (&mNvme, &mNvmeCount);
  if (EFI_ERROR (Status))
    return Status;
  if (mEmmc[0] != NULL)
    Print (L"Fastboot emmc0: %Lu MiB\r\n", Capacity (mEmmc[0]) >> 20);
  for (UINTN I = 0; I < mNvmeCount; I++)
    Print (L"Fastboot %a: %Lu MiB, PCI %x:%02x:%02x.%x, NS %u\r\n",
           mNvme[I].Name, Capacity (mNvme[I].Io) >> 20,
           (UINT32)mNvme[I].Segment, (UINT32)mNvme[I].Bus,
           (UINT32)mNvme[I].Device, (UINT32)mNvme[I].Function,
           mNvme[I].Namespace);
  return EFI_SUCCESS;
}

VOID
FastbootPlatformStop (VOID)
{
  ZeroMem (mEmmc, sizeof (mEmmc));
  if (mNvme != NULL)
    FreePool (mNvme);
  mNvme = NULL;
  mNvmeCount = 0;
}

EFI_STATUS
FastbootGetVar (CONST CHAR8 *Name, CHAR8 *Value, UINTN Size)
{
  CONST CHAR8 *Partition;
  EFI_BLOCK_IO_PROTOCOL *Io;

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
  Io = Target (Partition);
  if (Io == NULL)
    return EFI_NOT_FOUND;
  if (!AsciiStrnCmp (Name, "partition-size:", 15))
    AsciiSPrint (Value, Size, "0x%Lx", Capacity (Io));
  else
    AsciiStrCpyS (Value, Size,
                  !AsciiStrnCmp (Name, "has-slot:", 9) ? "no" : "raw");
  return EFI_SUCCESS;
}

EFI_STATUS
FastbootRead (CONST CHAR8 *Name, UINT64 Offset, UINTN Size, VOID *Data)
{
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_BLOCK_IO_PROTOCOL *Io = Target (Name);
  UINT8 *Bounce;
  UINTN BlockSize;

  if (Io == NULL)
    return EFI_NOT_FOUND;
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
  EFI_BLOCK_IO_PROTOCOL *Io = Target (Name);

  if (Io == NULL)
    return EFI_NOT_FOUND;
  return FastbootWriteImage (Io, Data, Size,
                             Io != mEmmc[1] && Io != mEmmc[2], NULL);
}
