// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520NvStoreLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

EFI_STATUS
Th1520NvRecord (CONST VOID *Data, UINTN Length, UINTN Offset,
                TH1520_NV_RECORD *R)
{
  CONST UINT8 *P = Data;
  if (!Data || !R || Offset > Length || Length - Offset < 30)
    return EFI_COMPROMISED_DATA;
  R->NameSize = ReadUnaligned32 (P + Offset);
  if (R->NameSize < 4 || (R->NameSize & 1) ||
      R->NameSize > Length - Offset - 28)
    return EFI_COMPROMISED_DATA;
  R->Name = P + Offset + 4;
  for (UINTN I = 0; I < R->NameSize; I += 2)
    if ((ReadUnaligned16 ((CONST UINT8 *)R->Name + I) == 0) !=
        (I == R->NameSize - 2))
      return EFI_COMPROMISED_DATA;
  R->Guid = (CONST UINT8 *)R->Name + R->NameSize;
  R->Attributes = ReadUnaligned32 ((CONST UINT8 *)R->Guid + 16);
  R->DataSize = ReadUnaligned32 ((CONST UINT8 *)R->Guid + 20);
  if (!R->DataSize || R->DataSize > Length - Offset - R->NameSize - 28 ||
      !(R->Attributes & EFI_VARIABLE_NON_VOLATILE) ||
      !(R->Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) ||
      (R->Attributes & ~0xfU))
    return EFI_COMPROMISED_DATA;
  R->Data = (CONST UINT8 *)R->Guid + 24;
  R->Size = R->NameSize + R->DataSize + 28;
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520NvValidate (CONST VOID *Data, UINTN Length)
{
  TH1520_NV_RECORD R;
  if (Length > TH1520_NV_DATA_SIZE)
    return EFI_BAD_BUFFER_SIZE;
  for (UINTN Offset = 0; Offset < Length; Offset += R.Size)
  {
    EFI_STATUS Status = Th1520NvRecord (Data, Length, Offset, &R);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520NvUpdate (VOID *Buffer, UINTN *Length, CONST CHAR16 *Name,
                CONST EFI_GUID *Guid, UINT32 Attributes, UINTN Size,
                CONST VOID *Data)
{
  TH1520_NV_RECORD R;
  UINTN Old = 0, At = 0, NameSize = StrSize (Name);
  UINTN Add = Size ? NameSize + Size + 28 : 0;
  UINT8 *P = Buffer;
  if (NameSize > TH1520_NV_DATA_SIZE || Size > TH1520_NV_DATA_SIZE ||
      Add > TH1520_NV_DATA_SIZE)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN Offset = 0; Offset < *Length; Offset += R.Size)
  {
    EFI_STATUS Status = Th1520NvRecord (Buffer, *Length, Offset, &R);
    if (EFI_ERROR (Status))
      return Status;
    if (R.NameSize == NameSize && !CompareMem (R.Name, Name, NameSize) &&
        !CompareMem (R.Guid, Guid, sizeof (*Guid)))
    {
      if (R.Attributes == Attributes && R.DataSize == Size &&
          !CompareMem (R.Data, Data, Size))
        return EFI_SUCCESS;
      At = Offset;
      Old = R.Size;
      break;
    }
  }
  if (*Length - Old > TH1520_NV_DATA_SIZE - Add)
    return EFI_OUT_OF_RESOURCES;
  if (Old)
  {
    CopyMem (P + At, P + At + Old, *Length - At - Old);
    *Length -= Old;
  }
  if (!Size)
    return EFI_SUCCESS;
  P += *Length;
  WriteUnaligned32 ((UINT32 *)P, (UINT32)NameSize);
  CopyMem (P + 4, Name, NameSize);
  P += 4 + NameSize;
  CopyMem (P, Guid, sizeof (*Guid));
  WriteUnaligned32 ((UINT32 *)(P + 16), Attributes);
  WriteUnaligned32 ((UINT32 *)(P + 20), (UINT32)Size);
  CopyMem (P + 24, Data, Size);
  *Length += Add;
  return EFI_SUCCESS;
}
