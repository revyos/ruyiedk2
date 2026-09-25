// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520NvStoreLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

STATIC BOOLEAN
Empty (CONST TH1520_NV_HEADER *Header)
{
  CONST UINT8 *P = (CONST UINT8 *)Header;
  if (*P != 0 && *P != 0xff)
    return FALSE;
  for (UINTN I = 1; I < sizeof (*Header); I++)
    if (P[I] != *P)
      return FALSE;
  return TRUE;
}

EFI_STATUS
Th1520NvLoad (TH1520_NV_STORE *Store, VOID *Buffer, UINTN *Length)
{
  TH1520_NV_HEADER Header;
  BOOLEAN Blank = FALSE, ReadFailed = FALSE, Committed = FALSE;
  EFI_STATUS Error = EFI_VOLUME_CORRUPTED;
  Store->Slot = -1;
  Store->Generation = 0;
  *Length = 0;
  for (INTN Slot = 0; Slot < 2; Slot++)
  {
    UINTN Offset = Store->Offset + Slot * TH1520_NV_SLOT_SIZE;
    EFI_STATUS Status = Store->Read (Offset, sizeof (Header), &Header);
    if (EFI_ERROR (Status))
    {
      Error = Status;
      ReadFailed = TRUE;
      continue;
    }
    Blank |= Empty (&Header);
    UINT32 Crc = Header.HeaderCrc;
    Header.HeaderCrc = 0;
    if (Header.Magic != TH1520_NV_MAGIC || Header.Version != 1 ||
        Header.Length > TH1520_NV_DATA_SIZE ||
        CalculateCrc32 (&Header, sizeof (Header)) != Crc)
      continue;
    Committed = TRUE;
    Status = Store->Read (Offset + sizeof (Header),
        ALIGN_VALUE (Header.Length, 512), Store->Scratch);
    if (EFI_ERROR (Status))
    {
      Error = Status;
      ReadFailed = TRUE;
      continue;
    }
    if (CalculateCrc32 (Store->Scratch, Header.Length) != Header.DataCrc ||
        EFI_ERROR (Th1520NvValidate (Store->Scratch, Header.Length)))
      continue;
    if (Store->Slot < 0 || Header.Generation > Store->Generation)
    {
      Store->Slot = Slot;
      Store->Generation = Header.Generation;
      *Length = Header.Length;
      CopyMem (Buffer, Store->Scratch, *Length);
    }
  }
  if (Store->Slot >= 0)
    return EFI_SUCCESS;
  return ReadFailed ? Error : Blank && !Committed ? EFI_NOT_FOUND : Error;
}

EFI_STATUS
Th1520NvSave (TH1520_NV_STORE *Store, CONST VOID *Buffer, UINTN Length)
{
  TH1520_NV_HEADER Header, Verify;
  UINTN Slot = Store->Slot == 0 ? 1 : 0;
  UINTN Offset = Store->Offset + Slot * TH1520_NV_SLOT_SIZE;
  UINTN Bytes = ALIGN_VALUE (Length, 512);
  EFI_STATUS Status = Th1520NvValidate (Buffer, Length);
  if (EFI_ERROR (Status))
    return Status;
  if (Store->Generation == MAX_UINT64)
    return EFI_VOLUME_FULL;
  SetMem (&Header, sizeof (Header), 0xff);
  Header.Magic = TH1520_NV_MAGIC;
  Header.Version = 1;
  Header.Length = (UINT32)Length;
  Header.Generation = Store->Generation + 1;
  Header.DataCrc = CalculateCrc32 ((VOID *)Buffer, Length);
  Header.HeaderCrc = 0;
  Header.HeaderCrc = CalculateCrc32 (&Header, sizeof (Header));
  Status = Store->Erase (Offset, ALIGN_VALUE (Bytes + 512, SIZE_4KB));
  if (EFI_ERROR (Status))
    return Status;
  SetMem (Store->Scratch, Bytes, 0xff);
  CopyMem (Store->Scratch, Buffer, Length);
  if (Bytes)
    Status = Store->Write (Offset + 512, Bytes, Store->Scratch);
  if (!EFI_ERROR (Status) && Bytes)
    Status = Store->Read (Offset + 512, Bytes, Store->Scratch);
  if (!EFI_ERROR (Status) && CompareMem (Buffer, Store->Scratch, Length))
    Status = EFI_CRC_ERROR;
  if (!EFI_ERROR (Status))
    Status = Store->Write (Offset, sizeof (Header), &Header);
  if (!EFI_ERROR (Status))
    Status = Store->Read (Offset, sizeof (Verify), &Verify);
  if (!EFI_ERROR (Status) &&
      CompareMem (&Header, &Verify, sizeof (Header)))
    Status = EFI_CRC_ERROR;
  if (!EFI_ERROR (Status))
  {
    Store->Slot = Slot;
    Store->Generation = Header.Generation;
  }
  return Status;
}
