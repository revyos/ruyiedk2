// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520MacStoreLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
static UINT8 Media[TH1520_MAC_REGION_SIZE];
static unsigned Writes;
static int FailCopy = -1;
static BOOLEAN Torn;
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memcpy (D, S, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI SetMem (VOID *P, UINTN N, UINT8 V)
{ return memset (P, V, N); }
UINT32 EFIAPI CalculateCrc32 (VOID *P, UINTN N)
{ return crc32 (0, P, N); }
static EFI_STATUS Read (UINTN Offset, VOID *Block)
{
  assert (Offset == 0 || Offset == TH1520_MAC_COPY_STRIDE);
  memcpy (Block, Media + Offset, 512);
  return EFI_SUCCESS;
}
static EFI_STATUS Write (UINTN Offset, CONST VOID *Block)
{
  assert (Offset == 0 || Offset == TH1520_MAC_COPY_STRIDE);
  Writes++;
  if ((int)(Offset / TH1520_MAC_COPY_STRIDE) == FailCopy)
    return EFI_DEVICE_ERROR;
  if (Torn)
  {
    memcpy (Media + Offset, Block, 16);
    return EFI_DEVICE_ERROR;
  }
  memcpy (Media + Offset, Block, 512);
  return EFI_SUCCESS;
}
int main (void)
{
  static const UINT8 Invalid[][6] = {
    {0}, {3, 0x11, 0x22, 0x33, 0x44, 0x82},
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
  };
  const UINT8 Mac[6] = {2, 0x11, 0x22, 0x33, 0x44, 0x80};
  const UINT8 Other[6] = {2, 0x11, 0x22, 0x33, 0x44, 0x82};
  const UINT8 Multicast[6] = {3, 0x11, 0x22, 0x33, 0x44, 0x82};
  UINT8 Got[6];
  memset (Media, 0xff, sizeof (Media));
  assert (Th1520MacRead (Read, Got) == EFI_NOT_FOUND);
  assert (Th1520MacWrite (Read, Write, Multicast) ==
          EFI_INVALID_PARAMETER);
  assert (!Writes);
  assert (Th1520MacWrite (Read, Write, Mac) == EFI_SUCCESS);
  assert (Writes == 2);
  assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
  assert (!memcmp (Got, Mac, 6));
  assert (Th1520MacWrite (Read, Write, Mac) == EFI_SUCCESS);
  assert (Writes == 2);
  assert (Th1520MacWrite (Read, Write, Other) == EFI_WRITE_PROTECTED);
  UINT8 Good[512];
  memcpy (Good, Media, sizeof (Good));
  for (unsigned Bad = 0; Bad < sizeof (Invalid) / 6; Bad++)
  {
    // Even a valid CRC must not make an invalid MAC usable.
    memcpy (Media, Good, sizeof (Good));
    memcpy (Media + TH1520_MAC_COPY_STRIDE, Good, sizeof (Good));
    memcpy (Media + 8, Invalid[Bad], 6);
    memset (Media + 16, 0, 4);
    UINT32 Crc = CalculateCrc32 (Media, 512);
    memcpy (Media + 16, &Crc, sizeof (Crc));
    assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
    assert (!memcmp (Got, Mac, 6));
    memcpy (Media + TH1520_MAC_COPY_STRIDE, Media, 512);
    assert (Th1520MacRead (Read, Got) == EFI_NOT_FOUND);
    assert (Th1520MacWrite (Read, Write, Mac) == EFI_SUCCESS);
  }
  Media[0] ^= 1;
  assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
  assert (!memcmp (Got, Mac, 6));
  for (int Copy = 0; Copy < 2; Copy++)
  {
    memset (Media, 0xff, sizeof (Media));
    FailCopy = Copy;
    assert (Th1520MacWrite (Read, Write, Mac) == EFI_SUCCESS);
    assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
    assert (!memcmp (Got, Mac, 6));
  }
  FailCopy = -1;
  TH1520_MAC_SETTINGS Settings = {.OverrideMask = 1};
  TH1520_MAC_SETTINGS Loaded;
  memcpy (Settings.Address[0], Other, 6);
  // Simulate an existing version 1 record, including its padding bytes.
  memcpy (Media, Good, sizeof (Good));
  UINT32 Version = 1, Crc;
  memcpy (Media + 4, &Version, 4);
  memset (Media + 20, 0xff, 492);
  memset (Media + 16, 0, 4);
  Crc = CalculateCrc32 (Media, 512);
  memcpy (Media + 16, &Crc, 4);
  memcpy (Media + TH1520_MAC_COPY_STRIDE, Media, 512);
  assert (Th1520MacWriteSettings (Read, Write, &Settings) == EFI_SUCCESS);
  assert (Th1520MacReadSettings (Read, &Loaded) == EFI_SUCCESS);
  assert (!memcmp (&Loaded, &Settings, sizeof (Loaded)));
  assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
  assert (!memcmp (Got, Mac, 6));
  Settings.Address[0][5] += 2;
  Torn = TRUE;
  assert (Th1520MacWriteSettings (Read, Write, &Settings) ==
          EFI_DEVICE_ERROR);
  assert (Th1520MacReadSettings (Read, &Loaded) == EFI_SUCCESS);
  assert (Loaded.Address[0][5] == Other[5]);
  Torn = FALSE;
  assert (Th1520MacWriteSettings (Read, Write, &Settings) == EFI_SUCCESS);
  assert (Th1520MacReadSettings (Read, &Loaded) == EFI_SUCCESS);
  assert (!memcmp (&Loaded, &Settings, sizeof (Loaded)));
  memset (&Settings, 0, sizeof (Settings));
  assert (Th1520MacWriteSettings (Read, Write, &Settings) == EFI_SUCCESS);
  assert (Th1520MacReadSettings (Read, &Loaded) == EFI_SUCCESS);
  assert (!Loaded.OverrideMask);
  assert (Th1520MacRead (Read, Got) == EFI_SUCCESS);
  assert (!memcmp (Got, Mac, 6));
  // An override may be saved before a fallback has ever been generated.
  memset (Media, 0xff, sizeof (Media));
  Settings.OverrideMask = 1;
  memcpy (Settings.Address[0], Other, 6);
  assert (Th1520MacWriteSettings (Read, Write, &Settings) == EFI_SUCCESS);
  assert (Th1520MacRead (Read, Got) == EFI_NOT_FOUND);
  assert (Th1520MacWrite (Read, Write, Mac) == EFI_SUCCESS);
  assert (Th1520MacReadSettings (Read, &Loaded) == EFI_SUCCESS);
  assert (!memcmp (&Loaded, &Settings, sizeof (Loaded)));
  Settings.Address[0][0] = 1;
  unsigned Before = Writes;
  assert (Th1520MacWriteSettings (Read, Write, &Settings) ==
          EFI_INVALID_PARAMETER);
  assert (Writes == Before);
  puts ("boot0 settings: migration, torn writes and restore passed");
  puts ("boot0 MAC: invalid addresses, dual copies, corruption recovery "
        "and write-once passed");
}
