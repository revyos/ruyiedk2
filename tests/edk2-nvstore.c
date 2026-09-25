// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520NvStoreLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static UINT8 Flash[TH1520_NV_REGION_SIZE], Initial[sizeof (Flash)];
static UINT8 Data[TH1520_NV_DATA_SIZE], Loaded[sizeof (Data)];
static UINT8 Scratch[sizeof (Data)];
static int Cut = -1, Actions;
static BOOLEAN ReadFails;
UINT16 EFIAPI ReadUnaligned16 (CONST VOID *P)
{ UINT16 V; memcpy (&V, P, 2); return V; }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P)
{ UINT32 V; memcpy (&V, P, 4); return V; }
UINT32 EFIAPI WriteUnaligned32 (VOID *P, UINT32 V)
{ memcpy (P, &V, 4); return V; }
UINTN EFIAPI StrSize (CONST CHAR16 *S)
{ UINTN I = 0; while (S[I]) I++; return (I + 1) * 2; }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memmove (D, S, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI SetMem (VOID *P, UINTN N, UINT8 V)
{ return memset (P, V, N); }
UINT32 EFIAPI CalculateCrc32 (VOID *P, UINTN N)
{ return crc32 (0, P, N); }

static EFI_STATUS Read (UINTN Offset, UINTN Size, VOID *Buffer)
{
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  if (ReadFails && Offset == 0)
    return EFI_DEVICE_ERROR;
  memcpy (Buffer, Flash + Offset, Size);
  return EFI_SUCCESS;
}
static EFI_STATUS Write (UINTN Offset, UINTN Size, CONST VOID *Buffer)
{
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  for (UINTN At = 0; At < Size; At += 256)
  {
    if (Actions++ == Cut)
      return EFI_DEVICE_ERROR;
    UINTN Count = MIN (Size - At, 256);
    for (UINTN I = 0; I < Count; I++)
    {
      UINT8 Value = ((UINT8 *)Buffer)[At + I];
      assert ((Flash[Offset + At + I] & Value) == Value);
      Flash[Offset + At + I] = Value;
    }
  }
  return EFI_SUCCESS;
}
static EFI_STATUS Erase (UINTN Offset, UINTN Size)
{
  assert (!(Offset % SIZE_4KB) && !(Size % SIZE_4KB));
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  for (UINTN At = 0; At < Size; At += SIZE_4KB)
  {
    if (Actions++ == Cut)
      return EFI_DEVICE_ERROR;
    memset (Flash + Offset + At, 0xff, SIZE_4KB);
  }
  return EFI_SUCCESS;
}
int main (void)
{
  EFI_GUID Guid = {0x12345678};
  UINTN Length = 0, Size;
  TH1520_NV_STORE Store = {
    .Read = Read, .Write = Write, .Erase = Erase, .Scratch = Scratch
  };
  memset (Flash, 0xff, sizeof (Flash));
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_NOT_FOUND);
  assert (Th1520NvUpdate (Data, &Length, L"Test", &Guid,
                          7, 4, "old") == EFI_SUCCESS);
  assert (Th1520NvSave (&Store, Data, Length) == EFI_SUCCESS);
  assert (Store.Slot == 0 && Store.Generation == 1);
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_SUCCESS);
  assert (Size == Length && !memcmp (Data, Loaded, Size));
  memcpy (Initial, Flash, sizeof (Flash));
  assert (Th1520NvUpdate (Data, &Length, L"Test", &Guid,
                          7, 4, "new") == EFI_SUCCESS);
  // Cut power at each erase/page boundary. Only complete versions survive.
  for (int Failure = 0; Failure < 6; Failure++)
  {
    memcpy (Flash, Initial, sizeof (Flash));
    Cut = -1;
    assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_SUCCESS);
    Cut = Failure;
    Actions = 0;
    Th1520NvSave (&Store, Data, Length);
    Cut = -1;
    assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_SUCCESS);
    TH1520_NV_RECORD R;
    assert (Th1520NvRecord (Loaded, Size, 0, &R) == EFI_SUCCESS);
    assert (!memcmp (R.Data, "old", 4) || !memcmp (R.Data, "new", 4));
  }
  memcpy (Flash, Initial, sizeof (Flash));
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_SUCCESS);
  assert (Th1520NvSave (&Store, Data, Length) == EFI_SUCCESS);
  Flash[TH1520_NV_SLOT_SIZE + 512] ^= 1;
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_SUCCESS);
  assert (Store.Generation == 1);
  // A failed read must not turn a populated store into a new empty store.
  memcpy (Flash, Initial, sizeof (Flash));
  ReadFails = TRUE;
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_DEVICE_ERROR);
  ReadFails = FALSE;
  Flash[512] ^= 1;
  // A committed but corrupt bank is not an empty variable store.
  assert (Th1520NvLoad (&Store, Loaded, &Size) == EFI_VOLUME_CORRUPTED);
  assert (Th1520NvValidate (Data, 3) == EFI_COMPROMISED_DATA);
  Data[4] = Data[5] = 0;
  int Before = Actions;
  assert (Th1520NvSave (&Store, Data, Length) == EFI_COMPROMISED_DATA);
  assert (Actions == Before);
  Length = 0;
  assert (Th1520NvUpdate (Data, &Length, L"Empty", &Guid,
                          7, 4, "abc") == EFI_SUCCESS);
  assert (Th1520NvUpdate (Data, &Length, L"Empty", &Guid,
                          0, 0, NULL) == EFI_SUCCESS && !Length);
  puts ("NV store: atomic slots, corruption, read errors "
        "and deletion passed");
}
