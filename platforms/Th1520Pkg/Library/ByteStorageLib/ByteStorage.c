// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520ByteStorageLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

EFI_STATUS
Th1520StorageBytes (UINT64 Capacity, UINTN BlockSize,
                   TH1520_ALIGNED_STORAGE_IO Transfer, UINT64 Offset,
                   UINTN Length, VOID *Buffer, BOOLEAN Write)
{
  UINT8 *Block, *Verify = NULL, *Data = Buffer;
  UINTN Prefix, Count, Padded;
  EFI_STATUS Status = EFI_SUCCESS;
  if (Transfer == NULL || BlockSize == 0 || BlockSize > SIZE_64KB ||
      SIZE_64KB % BlockSize || Capacity % BlockSize || Offset > Capacity ||
      Length > Capacity - Offset || (Length && Buffer == NULL))
    return EFI_INVALID_PARAMETER;
  if (Length == 0)
    return EFI_SUCCESS;
  Block = AllocatePool (SIZE_64KB);
  if (Write)
    Verify = AllocatePool (SIZE_64KB);
  if (Block == NULL || (Write && Verify == NULL))
  {
    if (Block != NULL) FreePool (Block);
    if (Verify != NULL) FreePool (Verify);
    return EFI_OUT_OF_RESOURCES;
  }
  while (Length)
  {
    Prefix = (UINTN)(Offset % BlockSize);
    Count = MIN (Length, SIZE_64KB - Prefix);
    Padded = ((Prefix + Count + BlockSize - 1) / BlockSize) * BlockSize;
    if (!Write || Prefix || Count % BlockSize)
    {
      Status = Transfer (Offset - Prefix, Padded, Block, FALSE);
      if (EFI_ERROR (Status)) break;
    }
    if (Write)
    {
      CopyMem (Block + Prefix, Data, Count);
      Status = Transfer (Offset - Prefix, Padded, Block, TRUE);
      if (!EFI_ERROR (Status))
        Status = Transfer (Offset - Prefix, Padded, Verify, FALSE);
      if (!EFI_ERROR (Status) && CompareMem (Block, Verify, Padded))
        Status = EFI_CRC_ERROR;
      if (EFI_ERROR (Status)) break;
    }
    else
      CopyMem (Data, Block + Prefix, Count);
    Offset += Count;
    Data += Count;
    Length -= Count;
  }
  if (Verify != NULL) FreePool (Verify);
  FreePool (Block);
  return Status;
}
