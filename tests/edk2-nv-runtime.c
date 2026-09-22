// SPDX-License-Identifier: BSD-2-Clause-Patent
#define _PCD_GET_MODE_BOOL_PcdFirmwareOnEmmc 0
#include "../platforms/Th1520Pkg/Drivers/NvStoreDxe/NvStore.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
static NV_CONTEXT Context;
static UINT8 Flash[TH1520_NV_REGION_SIZE];
static CHAR8 Current[16] = "old";
static unsigned Sets, Inits, Writes;
static BOOLEAN FailWrite;
EFI_GUID gTh1520NvStoreGuid = {1};
static EFI_GUID TestGuid = {2};
BOOLEAN EFIAPI EfiAtRuntime (VOID) { return TRUE; }
BOOLEAN EFIAPI CompareGuid (CONST GUID *A, CONST GUID *B)
{ return !memcmp (A, B, sizeof (*A)); }
INTN EFIAPI StrCmp (CONST CHAR16 *A, CONST CHAR16 *B)
{ while (*A && *A == *B) { A++; B++; } return *A - *B; }
UINTN EFIAPI StrSize (CONST CHAR16 *S)
{ UINTN N = 0; while (S[N]) N++; return (N + 1) * 2; }
UINT16 EFIAPI ReadUnaligned16 (CONST VOID *P)
{ UINT16 V; memcpy (&V, P, 2); return V; }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P)
{ UINT32 V; memcpy (&V, P, 4); return V; }
UINT32 EFIAPI WriteUnaligned32 (VOID *P, UINT32 V)
{ memcpy (P, &V, 4); return V; }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memmove (D, S, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI SetMem (VOID *P, UINTN N, UINT8 V)
{ return memset (P, V, N); }
UINT32 EFIAPI CalculateCrc32 (VOID *P, UINTN N)
{ return crc32 (0, P, N); }
EFI_STATUS Th1520SpiRuntimeInit (VOID) { Inits++; return EFI_SUCCESS; }
EFI_STATUS Th1520SpiInitNative (VOID) { assert (0); return EFI_ABORTED; }
static EFI_STATUS EFIAPI
Get (CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attrs, UINTN *Size, VOID *Data)
{
  if (StrCmp (Name, L"Test")) return EFI_NOT_FOUND;
  *Attrs = 7;
  *Size = strlen (Current) + 1;
  memcpy (Data, Current, *Size);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Next (UINTN *Size, CHAR16 *Name, EFI_GUID *Guid)
{
  if (*Name) return EFI_NOT_FOUND;
  memcpy (Name, L"Test", sizeof (L"Test"));
  *Size = sizeof (L"Test");
  *Guid = TestGuid;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Update (CHAR16 *Name, EFI_GUID *Guid, UINT32 Attrs,
        UINTN Size, VOID *Data)
{
  assert (!StrCmp (Name, L"Test") && Size <= sizeof (Current));
  Sets++;
  memset (Current, 0, sizeof (Current));
  memcpy (Current, Data, Size);
  return EFI_SUCCESS;
}
static EFI_STATUS Read (UINTN Offset, UINTN Size, VOID *Data)
{
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  memcpy (Data, Flash + Offset, Size);
  return EFI_SUCCESS;
}
static EFI_STATUS Write (UINTN Offset, UINTN Size, CONST VOID *Data)
{
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  Writes++;
  if (FailWrite) return EFI_DEVICE_ERROR;
  memcpy (Flash + Offset, Data, Size);
  return EFI_SUCCESS;
}
static EFI_STATUS Erase (UINTN Offset, UINTN Size)
{
  assert (Offset <= sizeof (Flash) && Size <= sizeof (Flash) - Offset);
  memset (Flash + Offset, 0xff, Size);
  return EFI_SUCCESS;
}
// No boot service may be called during these tests.
EFI_BOOT_SERVICES *gBS;
int main (void)
{
  UINT8 Mac[6] = {2, 3, 4, 5, 6, 8};
  TH1520_NV_RECORD R;
  memset (Flash, 0xff, sizeof (Flash));
  Nv = &Context;
  Nv->Status = EFI_SUCCESS;
  Nv->Store = (TH1520_NV_STORE){
    .Read = Read, .Write = Write, .Erase = Erase,
    .Scratch = Nv->Scratch, .Slot = -1
  };
  OriginalGet = Get;
  OriginalSet = Update;
  OriginalNext = Next;
  assert (Th1520NvUpdate (Nv->Cache, &Nv->Length, L"FallbackMac",
      &gTh1520NvStoreGuid, 3, 6, Mac) == EFI_SUCCESS);
  assert (Th1520NvUpdate (Nv->Cache, &Nv->Length, L"Test",
      &TestGuid, 7, 4, "old") == EFI_SUCCESS);
  assert (SetVariable (L"FallbackMac", &gTh1520NvStoreGuid,
      7, 6, Mac) == EFI_WRITE_PROTECTED);
  assert (SetVariable (L"MacSettings", &gTh1520NvStoreGuid,
      7, 6, Mac) == EFI_WRITE_PROTECTED);
  assert (!Sets && !Inits && !Writes);
  assert (SetVariable (L"Test", &TestGuid, 7, 4, "new") == EFI_SUCCESS);
  assert (!strcmp (Current, "new") && Sets == 1 && Inits == 1);
  assert (Th1520NvRecord (Nv->Cache, Nv->Length, 0, &R) == EFI_SUCCESS);
  assert (R.Attributes == 3 && R.DataSize == 6);
  assert (!memcmp (R.Data, Mac, 6));
  unsigned Before = Writes;
  assert (SetVariable (L"Test", &TestGuid, 7, 4, "new") == EFI_SUCCESS);
  assert (Writes == Before); // Avoid erasing flash for unchanged values.
  FailWrite = TRUE;
  Before = Sets;
  assert (SetVariable (L"Test", &TestGuid, 7, 4, "bad") ==
          EFI_DEVICE_ERROR);
  assert (!strcmp (Current, "new") && Sets == Before + 2);
  assert (Nv->Store.Generation == 1);
  puts ("NV runtime: hidden MAC, BS-only preservation "
        "and rollback passed");
}
