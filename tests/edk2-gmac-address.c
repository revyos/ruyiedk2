// SPDX-License-Identifier: BSD-2-Clause-Patent
// One random pair per boot, independent of DT probe order.
#define _PCD_GET_MODE_BOOL_PcdFirmwareOnEmmc 0
#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/RngLib.h>
#include <Library/TimerLib.h>
#include <Protocol/VariableLock.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "../platforms/Th1520Pkg/Drivers/GmacDxe/Gmac.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned Calls;
static BOOLEAN RandomWorks, HashWorks;
BOOLEAN EFIAPI GetRandomNumber64 (UINT64 *Value)
{ Calls++; *Value = 7; return RandomWorks; }
UINT64 EFIAPI GetPerformanceCounter (VOID) { return 42; }
BOOLEAN EFIAPI Sha256HashAll (CONST VOID *Data, UINTN Size, UINT8 *Digest)
{
  assert (Size == 32);
  memset (Digest, 0xff, SHA256_DIGEST_SIZE);
  return HashWorks;
}
VOID *EFIAPI CopyMem (VOID *Dst, CONST VOID *Src, UINTN Size)
{ return memcpy (Dst, Src, Size); }
EFI_GUID gTh1520NvStoreGuid;
static unsigned Saves;
static BOOLEAN HasSaved;
static UINT8 Saved[6] = {2, 3, 4, 5, 6, 8};
static UINTN SavedSize = sizeof (Saved);
static UINT32 SavedAttributes = EFI_VARIABLE_NON_VOLATILE;
static EFI_STATUS EFIAPI
GetVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attr,
             UINTN *Size, VOID *Data)
{
  if (!HasSaved)
    return EFI_NOT_FOUND;
  assert (*Size == sizeof (Saved));
  *Attr = SavedAttributes;
  *Size = SavedSize;
  if (SavedSize > sizeof (Saved))
    return EFI_BUFFER_TOO_SMALL;
  memcpy (Data, Saved, SavedSize);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
SetVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 Attr,
             UINTN Size, VOID *Data)
{
  assert (Attr == 3 && Size == 6);
  Saves++;
  return EFI_SUCCESS;
}
static EFI_RUNTIME_SERVICES Runtime = {
  .GetVariable = GetVariable, .SetVariable = SetVariable
};
EFI_RUNTIME_SERVICES *gRT = &Runtime;
EFI_GUID gEdkiiVariableLockProtocolGuid;
static unsigned Locks;
static EFI_STATUS EFIAPI
LockVariable (CONST EDKII_VARIABLE_LOCK_PROTOCOL *This,
              CHAR16 *Name, EFI_GUID *Guid)
{
  Locks++;
  return EFI_SUCCESS;
}
static EDKII_VARIABLE_LOCK_PROTOCOL Lock = {LockVariable};
static EFI_STATUS EFIAPI
Locate (EFI_GUID *Guid, VOID *Registration, VOID **Protocol)
{
  *Protocol = &Lock;
  return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES Services = {.LocateProtocol = Locate};
EFI_BOOT_SERVICES *gBS = &Services;
int main (int Argc, char **Argv)
{
  UINT8 First[6], Second[6], Again[6];
  if (Argc > 1)
  {
    HasSaved = TRUE;
    BOOLEAN Valid = !strcmp (Argv[1], "saved");
    if (!strcmp (Argv[1], "zero"))
      memset (Saved, 0, sizeof (Saved));
    else if (!strcmp (Argv[1], "broadcast"))
      memset (Saved, 0xff, sizeof (Saved));
    else if (!strcmp (Argv[1], "multicast"))
      Saved[0] = 3;
    else if (!strcmp (Argv[1], "short"))
      SavedSize = 5;
    else if (!strcmp (Argv[1], "long"))
      SavedSize = 7;
    else if (!strcmp (Argv[1], "empty"))
      SavedSize = 0;
    else if (!strcmp (Argv[1], "volatile"))
      SavedAttributes = 0;
    else
      assert (Valid);
    RandomWorks = HashWorks = TRUE;
    assert (GmacFallbackAddress (1, Second) == EFI_SUCCESS);
    assert (GmacFallbackAddress (0, First) == EFI_SUCCESS);
    if (Valid)
      assert (First[5] == 8 && Second[5] == 9 && !Calls && !Saves);
    else
    {
      assert (Calls == 1 && Saves == 1);
      assert (First[0] == 0xfe && First[5] == 0xfe);
      assert (Second[0] == 0xfe && Second[5] == 0xff);
    }
    assert (Locks == 1);
    printf ("GMAC saved address: %s passed\n", Argv[1]);
    return 0;
  }
  assert (GmacFallbackAddress (2, First) == EFI_INVALID_PARAMETER);
  assert (GmacFallbackAddress (0, NULL) == EFI_INVALID_PARAMETER);
  assert (Calls == 0);
  assert (GmacFallbackAddress (1, Second) == EFI_DEVICE_ERROR);
  RandomWorks = TRUE;
  assert (GmacFallbackAddress (0, First) == EFI_DEVICE_ERROR);
  HashWorks = TRUE;
  // Probe GMAC1 before GMAC0, as in the current LPi4A DTB.
  assert (GmacFallbackAddress (1, Second) == EFI_SUCCESS);
  assert (GmacFallbackAddress (0, First) == EFI_SUCCESS);
  assert (Calls == 3 && Saves == 1 && Locks == 1);
  assert (!memcmp (First, Second, 5));
  assert (First[5] == 0xfe && Second[5] == 0xff);
  assert ((First[0] & 3) == 2 && (Second[0] & 3) == 2);
  assert (GmacFallbackAddress (1, Again) == EFI_SUCCESS);
  assert (!memcmp (Again, Second, 6) && Calls == 3);
  puts ("GMAC MAC pair: reverse probe order, overflow boundary "
        "and RNG failures passed");
}
