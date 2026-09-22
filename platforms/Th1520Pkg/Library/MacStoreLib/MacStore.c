// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520MacStoreLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define HAS_FALLBACK BIT0
// Keep the version 1 header layout so existing saved MACs can be read.
typedef struct {
  UINT32 Magic, Version;
  UINT8 Mac[6], Reserved[2];
  UINT32 Crc, Flags;
  UINT64 Generation;
  TH1520_MAC_SETTINGS Settings;
  UINT8 Padding[464];
} MAC_BLOCK;
STATIC_ASSERT (sizeof (MAC_BLOCK) == 512, "MAC record is one sector");

BOOLEAN
Th1520MacValid (CONST UINT8 *Mac)
{
  STATIC CONST UINT8 Zero[6];
  return Mac && !(Mac[0] & 1) && CompareMem (Mac, Zero, 6);
}

BOOLEAN
Th1520MacSettingsValid (CONST TH1520_MAC_SETTINGS *Settings)
{
  if (!Settings || (Settings->OverrideMask & ~3U))
    return FALSE;
  for (UINTN I = 0; I < 2; I++)
    if ((Settings->OverrideMask & (1U << I)) &&
        !Th1520MacValid (Settings->Address[I]))
      return FALSE;
  return TRUE;
}

STATIC BOOLEAN
Valid (MAC_BLOCK *Block)
{
  UINT32 Crc = Block->Crc;
  Block->Crc = 0;
  if (Block->Magic != SIGNATURE_32 ('T','M','A','C') ||
      (Block->Version != 1 && Block->Version != 2) ||
      CalculateCrc32 (Block, sizeof (*Block)) != Crc)
    return FALSE;
  if (Block->Version == 1)
  {
    Block->Flags = HAS_FALLBACK;
    Block->Generation = 0;
    SetMem (&Block->Settings, sizeof (Block->Settings), 0);
  }
  return !(Block->Flags & ~HAS_FALLBACK) &&
      (!(Block->Flags & HAS_FALLBACK) ||
       ((Block->Mac[0] & 3) == 2 && !(Block->Mac[5] & 1)));
}

STATIC EFI_STATUS
ReadLatest (TH1520_MAC_READ Read, MAC_BLOCK *Latest, INTN *Slot)
{
  MAC_BLOCK Block;
  EFI_STATUS Error = EFI_NOT_FOUND;
  *Slot = -1;
  for (UINTN I = 0; I < 2; I++)
  {
    EFI_STATUS Status = Read (I * TH1520_MAC_COPY_STRIDE, &Block);
    if (EFI_ERROR (Status))
      Error = Status;
    else if (Valid (&Block) &&
             (*Slot < 0 || Block.Generation > Latest->Generation))
    {
      CopyMem (Latest, &Block, sizeof (Block));
      *Slot = (INTN)I;
    }
  }
  return *Slot >= 0 ? EFI_SUCCESS : Error;
}

STATIC EFI_STATUS
Save (TH1520_MAC_READ Read, TH1520_MAC_WRITE Write,
      MAC_BLOCK *Block, INTN OldSlot)
{
  MAC_BLOCK Verify;
  BOOLEAN Saved = FALSE;
  if (Block->Generation == MAX_UINT64)
    return EFI_VOLUME_FULL;
  Block->Magic = SIGNATURE_32 ('T','M','A','C');
  Block->Version = 2;
  Block->Generation++;
  Block->Crc = 0;
  Block->Crc = CalculateCrc32 (Block, sizeof (*Block));
  // Updates use the other bank. The first record gets two valid copies.
  for (UINTN I = 0; I < 2; I++)
  {
    if ((INTN)I == OldSlot)
      continue;
    UINTN Offset = I * TH1520_MAC_COPY_STRIDE;
    EFI_STATUS Status = Write (Offset, Block);
    if (!EFI_ERROR (Status))
      Status = Read (Offset, &Verify);
    if (!EFI_ERROR (Status) &&
        !CompareMem (Block, &Verify, sizeof (*Block)))
      Saved = TRUE;
  }
  return Saved ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS
LoadOrCreate (TH1520_MAC_READ Read, MAC_BLOCK *Block, INTN *Slot)
{
  EFI_STATUS Status = ReadLatest (Read, Block, Slot);
  if (Status == EFI_NOT_FOUND)
  {
    SetMem (Block, sizeof (*Block), 0);
    return EFI_SUCCESS;
  }
  return Status;
}

EFI_STATUS
Th1520MacRead (TH1520_MAC_READ Read, UINT8 *Mac)
{
  MAC_BLOCK Block;
  INTN Slot;
  EFI_STATUS Status;
  if (!Read || !Mac)
    return EFI_INVALID_PARAMETER;
  Status = ReadLatest (Read, &Block, &Slot);
  if (EFI_ERROR (Status))
    return Status;
  if (!(Block.Flags & HAS_FALLBACK))
    return EFI_NOT_FOUND;
  CopyMem (Mac, Block.Mac, 6);
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520MacWrite (TH1520_MAC_READ Read, TH1520_MAC_WRITE Write,
                CONST UINT8 *Mac)
{
  MAC_BLOCK Block;
  INTN Slot;
  EFI_STATUS Status;
  if (!Read || !Write || !Mac || (Mac[0] & 3) != 2 || (Mac[5] & 1))
    return EFI_INVALID_PARAMETER;
  Status = LoadOrCreate (Read, &Block, &Slot);
  if (EFI_ERROR (Status))
    return Status;
  if (Block.Flags & HAS_FALLBACK)
    return CompareMem (Block.Mac, Mac, 6) ?
        EFI_WRITE_PROTECTED : EFI_SUCCESS;
  Block.Flags |= HAS_FALLBACK;
  CopyMem (Block.Mac, Mac, 6);
  return Save (Read, Write, &Block, Slot);
}

EFI_STATUS
Th1520MacReadSettings (TH1520_MAC_READ Read, TH1520_MAC_SETTINGS *Settings)
{
  MAC_BLOCK Block;
  INTN Slot;
  EFI_STATUS Status;
  if (!Read || !Settings)
    return EFI_INVALID_PARAMETER;
  Status = ReadLatest (Read, &Block, &Slot);
  if (!EFI_ERROR (Status))
    CopyMem (Settings, &Block.Settings, sizeof (*Settings));
  return Status;
}

EFI_STATUS
Th1520MacWriteSettings (TH1520_MAC_READ Read, TH1520_MAC_WRITE Write,
                        CONST TH1520_MAC_SETTINGS *Settings)
{
  MAC_BLOCK Block;
  INTN Slot;
  EFI_STATUS Status;
  if (!Read || !Write || !Th1520MacSettingsValid (Settings))
    return EFI_INVALID_PARAMETER;
  Status = LoadOrCreate (Read, &Block, &Slot);
  if (EFI_ERROR (Status))
    return Status;
  if (!CompareMem (&Block.Settings, Settings, sizeof (*Settings)))
    return EFI_SUCCESS;
  CopyMem (&Block.Settings, Settings, sizeof (*Settings));
  return Save (Read, Write, &Block, Slot);
}

EFI_STATUS
Th1520MacAdjacent (CONST UINT8 *Base, UINT8 *Mac)
{
  if (!Base || !Mac || (Base[0] & 1) ||
      !CompareMem (Base, (UINT8[6]){0}, 6))
    return EFI_INVALID_PARAMETER;
  CopyMem (Mac, Base, 6);
  for (INTN I = 5; I >= 0; I--)
    if (++Mac[I])
      break;
  if (((Mac[0] & 1) || !CompareMem (Mac, (UINT8[6]){0}, 6)))
  {
    // Carry into the first octet must not turn a unicast into multicast.
    CopyMem (Mac, Base, 6);
    for (INTN I = 5; I >= 0; I--)
      if (Mac[I]--)
        break;
  }
  return EFI_SUCCESS;
}
