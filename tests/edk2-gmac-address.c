// SPDX-License-Identifier: BSD-2-Clause-Patent
// One random pair per boot, independent of DT probe order.
#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/RngLib.h>
#include <Library/TimerLib.h>
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
{ assert (Size == 32); memset (Digest, 0xff, SHA256_DIGEST_SIZE); return HashWorks; }
VOID *EFIAPI CopyMem (VOID *Dst, CONST VOID *Src, UINTN Size)
{ return memcpy (Dst, Src, Size); }
int main (void)
{
  UINT8 First[6], Second[6], Again[6];
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
  assert (Calls == 3);
  assert (!memcmp (First, Second, 5));
  assert (First[5] == 0xfe && Second[5] == 0xff);
  assert ((First[0] & 3) == 2 && (Second[0] & 3) == 2);
  assert (GmacFallbackAddress (1, Again) == EFI_SUCCESS);
  assert (!memcmp (Again, Second, 6) && Calls == 3);
  puts ("GMAC MAC pair: reverse probe order, overflow boundary and RNG failures passed");
}
