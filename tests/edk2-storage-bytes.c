// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520ByteStorageLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static UINT8 Disk[SIZE_2MB], Before[SIZE_2MB], Input[70001], Output[70001];
static UINTN BlockSize, Calls, FailAt;
static BOOLEAN Corrupt;
VOID *EFIAPI AllocatePool (UINTN N) { return malloc (N); }
VOID EFIAPI FreePool (VOID *P) { free (P); }
VOID *EFIAPI CopyMem (VOID *A, CONST VOID *B, UINTN N) { return memcpy (A, B, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N) { return memcmp (A, B, N); }

static EFI_STATUS Transfer (UINT64 Offset, UINTN Length, VOID *Buffer, BOOLEAN Write)
{
  Calls++;
  assert (Offset % BlockSize == 0 && Length % BlockSize == 0);
  assert (Length <= SIZE_64KB && Offset + Length <= sizeof (Disk));
  if (FailAt && Calls == FailAt) return EFI_DEVICE_ERROR;
  if (Write) memcpy (Disk + Offset, Buffer, Length);
  else {
    memcpy (Buffer, Disk + Offset, Length);
    if (Corrupt) ((UINT8 *)Buffer)[0] ^= 1;
  }
  return EFI_SUCCESS;
}

int main (void)
{
  const UINTN Offsets[] = {0, 1, 511, 512, 4095, 4096, 65535, 65536};
  const UINTN Lengths[] = {1, 3, 4, 511, 512, 513, 4097, 65536, 70001};
  for (UINTN I = 0; I < sizeof (Input); I++) Input[I] = (UINT8)(I * 137 + 31);
  // Android sparse magic is data here, not a format to reject or expand.
  Input[0] = 0x3a; Input[1] = 0xff; Input[2] = 0x26; Input[3] = 0xed;
  for (BlockSize = 512; BlockSize <= 4096; BlockSize *= 8)
  {
    for (UINTN O = 0; O < ARRAY_SIZE (Offsets); O++)
      for (UINTN L = 0; L < ARRAY_SIZE (Lengths); L++)
      {
        UINTN Offset = Offsets[O], Length = Lengths[L];
        for (UINTN I = 0; I < sizeof (Disk); I++) Disk[I] = (UINT8)(I * 19 + 7);
        memcpy (Before, Disk, sizeof (Disk));
        assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
            Offset, Length, Input, TRUE) == EFI_SUCCESS);
        assert (!memcmp (Disk, Before, Offset));
        assert (!memcmp (Disk + Offset, Input, Length));
        assert (!memcmp (Disk + Offset + Length, Before + Offset + Length,
                        sizeof (Disk) - Offset - Length));
        memset (Output, 0, sizeof (Output));
        assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
            Offset, Length, Output, FALSE) == EFI_SUCCESS);
        assert (!memcmp (Output, Input, Length));
      }
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        sizeof (Disk) - 3, 3, Input, TRUE) == EFI_SUCCESS);
    Calls = 0;
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        MAX_UINT64, 1, Input, TRUE) == EFI_INVALID_PARAMETER);
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        sizeof (Disk) - 1, 2, Input, TRUE) == EFI_INVALID_PARAMETER);
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        0, 1, NULL, TRUE) == EFI_INVALID_PARAMETER);
    assert (Calls == 0);
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        sizeof (Disk), 0, NULL, TRUE) == EFI_SUCCESS);
    Calls = 0; FailAt = 1;
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        1, 100, Input, TRUE) == EFI_DEVICE_ERROR);
    assert (Calls == 1);
    FailAt = 0; Corrupt = TRUE;
    assert (Th1520StorageBytes (sizeof (Disk), BlockSize, Transfer,
        0, BlockSize, Input, TRUE) == EFI_CRC_ERROR);
    Corrupt = FALSE;
  }
  puts ("144 byte-range round trips, adjacent bytes, opaque payloads and I/O errors passed");
}
