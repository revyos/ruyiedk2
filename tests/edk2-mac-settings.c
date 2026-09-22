// SPDX-License-Identifier: BSD-2-Clause-Patent
#define _PCD_GET_MODE_BOOL_PcdFirmwareOnEmmc 0
#include "../platforms/Th1520Pkg/Drivers/GmacDxe/Address.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static TH1520_MAC_SETTINGS Stored;
static UINTN StoredSize = sizeof (Stored);
static UINT32 StoredAttributes = 3;
static EFI_STATUS ReadStatus;
EFI_GUID gTh1520NvStoreGuid;
VOID *EFIAPI SetMem (VOID *P, UINTN N, UINT8 V)
{ return memset (P, V, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
static EFI_STATUS EFIAPI
ReadVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attributes,
              UINTN *Size, VOID *Data)
{
  if (EFI_ERROR (ReadStatus)) return ReadStatus;
  *Attributes = StoredAttributes;
  if (*Size < StoredSize)
  {
    *Size = StoredSize;
    return EFI_BUFFER_TOO_SMALL;
  }
  *Size = StoredSize;
  memcpy (Data, &Stored, StoredSize);
  return EFI_SUCCESS;
}
static EFI_RUNTIME_SERVICES Services = {.GetVariable = ReadVariable};
EFI_RUNTIME_SERVICES *gRT = &Services;
int main (void)
{
  TH1520_MAC_SETTINGS Got, Empty = {0};
  Stored.OverrideMask = 3;
  Stored.Address[0][0] = 2;
  memset (Stored.Address[1], 0xff, 6);
  assert (GmacReadSettings (&Got) == EFI_SUCCESS);
  assert (Got.OverrideMask == 1 && Got.Address[0][0] == 2);
  for (StoredSize = 0; StoredSize <= sizeof (Stored) + 1; StoredSize++)
  {
    if (StoredSize == sizeof (Stored)) continue;
    assert (GmacReadSettings (&Got) == EFI_COMPROMISED_DATA);
    assert (!memcmp (&Got, &Empty, sizeof (Got)));
  }
  StoredSize = sizeof (Stored);
  StoredAttributes = 2;
  assert (GmacReadSettings (&Got) == EFI_COMPROMISED_DATA);
  assert (!memcmp (&Got, &Empty, sizeof (Got)));
  ReadStatus = EFI_DEVICE_ERROR;
  assert (GmacReadSettings (&Got) == EFI_DEVICE_ERROR);
  assert (!memcmp (&Got, &Empty, sizeof (Got)));
  puts ("MAC settings: malformed records cleared, I/O errors preserved");
}
