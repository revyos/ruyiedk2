// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Adma.h"

EFI_STATUS
EmmcBuildAdma (EMMC_ADMA_DESCRIPTOR *Table, UINTN Capacity,
               UINT64 Address, UINTN Size)
{
  UINTN Count = 0, Length;
  if (Table == NULL || Size == 0 || Size > EMMC_DMA_SIZE ||
      (Address & 3) || Address > MAX_UINT32 ||
      Size - 1 > MAX_UINT32 - Address)
  {
    return EFI_INVALID_PARAMETER;
  }
  while (Size)
  {
    if (Count == Capacity)
    {
      return EFI_BUFFER_TOO_SMALL;
    }
    // DWC MSHC must not cross a 128 MiB boundary in one descriptor.
    Length = MIN (Size, SIZE_128MB - (Address & (SIZE_128MB - 1)));
    Length = MIN (Length, SIZE_64KB);
    Table[Count].Attributes = BIT0 | BIT5; // Valid, transfer.
    Table[Count].Length = (UINT16)Length; // Zero encodes 64 KiB.
    Table[Count].Address = (UINT32)Address;
    Address += Length;
    Size -= Length;
    Count++;
  }
  Table[Count - 1].Attributes |= BIT1; // End.
  return EFI_SUCCESS;
}
