// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520MacStoreLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/RngLib.h>
#include <Library/TimerLib.h>

EFI_STATUS
Th1520MacGenerate (UINT8 *Mac)
{
  UINT64 Samples[4];
  UINT8 Digest[SHA256_DIGEST_SIZE];
  if (!Mac)
    return EFI_INVALID_PARAMETER;
  Samples[0] = GetPerformanceCounter ();
  if (!GetRandomNumber64 (&Samples[1]))
    return EFI_DEVICE_ERROR;
  Samples[2] = GetPerformanceCounter ();
  Samples[3] = 0xffe7070000ULL;
  // Timer RngLib is software randomness, not a hardware entropy source.
  if (!Sha256HashAll (Samples, sizeof (Samples), Digest))
    return EFI_DEVICE_ERROR;
  CopyMem (Mac, Digest, 6);
  Mac[0] = (Mac[0] & 0xfc) | 2;
  Mac[5] &= 0xfe;
  return EFI_SUCCESS;
}
