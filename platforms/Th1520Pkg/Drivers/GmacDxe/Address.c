// SPDX-License-Identifier: BSD-2-Clause-Patent
// TH1520 factory MAC slots: GMAC0 at 0xb0, GMAC1 at 0xb6 (vendor BSP).
#include "Gmac.h"
#include <Library/BaseCryptLib.h>
#include <Library/RngLib.h>

#define EFUSE_BASE 0xffff210000ULL
#define EFUSE_CONTROL 0x00
#define EFUSE_ADDRESS 0x40
#define EFUSE_CHECK0 0x50
#define EFUSE_STATUS 0x70
#define EFUSE_DATA 0x80
#define EFUSE_ERRORS 0x4f70
#define EFUSE_COMMAND_FIELDS (BIT0 | BIT1 | BIT2 | (31U << 8) | BIT13)

STATIC EFI_STATUS
WaitForRead (VOID)
{
  UINTN Retry;
  UINT32 Status;
  for (Retry = 0; Retry < 10000; Retry++)
  {
    Status = MmioRead32 (EFUSE_BASE + EFUSE_STATUS);
    if (!(Status & BIT0))
    {
      return (Status & EFUSE_ERRORS) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}
EFI_STATUS
GmacReadFactoryAddress (GMAC_HARDWARE *Hw, UINT8 *Mac)
{
  // These are the READ command check words, not permanent protection bits.
  // Only command 1 (READ) is used. Never touch WDATA or issue a lock command.
  STATIC CONST UINT32 ReadCheck[4] = {0xa4c8c6de, 0xf4d7fb08, 0xc3f981d0, 0x32224e05};
  EFI_STATUS Status;
  UINT32 Control, Value;
  UINTN Word, Index;
  UINTN Offset = Hw->Index ? 0xb6 : 0xb0;
  UINT8 Bytes[8];
  Control = MmioRead32 (EFUSE_BASE + EFUSE_CONTROL);
  if (Control & BIT14)
  {
    return EFI_NOT_READY;
  }
  Control &= ~EFUSE_COMMAND_FIELDS;
  for (Word = 0; Word < 2; Word++)
  {
    Status = WaitForRead ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    for (Index = 0; Index < ARRAY_SIZE (ReadCheck); Index++)
    {
      MmioWrite32 (EFUSE_BASE + EFUSE_CHECK0 + Index * 4, ReadCheck[Index]);
    }
    MmioWrite32 (EFUSE_BASE + EFUSE_ADDRESS, Offset / 4 + Word);
    MmioWrite32 (EFUSE_BASE + EFUSE_CONTROL, Control | (1U << 8) | BIT0);
    Status = WaitForRead ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Value = MmioRead32 (EFUSE_BASE + EFUSE_DATA);
    for (Index = 0; Index < 4; Index++)
    {
      Bytes[Word * 4 + Index] = (UINT8)(Value >> (Index * 8));
    }
    // Clear the volatile read buffer with START unset.
    MmioWrite32 (EFUSE_BASE + EFUSE_CONTROL, Control | BIT1);
  }
  CopyMem (Mac, Bytes + Offset % 4, 6);
  return EFI_SUCCESS;
}

EFI_STATUS
GmacFallbackAddress (UINTN Index, UINT8 *Mac)
{
  STATIC UINT8 Base[6];
  STATIC BOOLEAN Ready;
  UINT64 Samples[4];
  UINT8 Digest[SHA256_DIGEST_SIZE];

  if (Index > 1 || Mac == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!Ready)
  {
    Samples[0] = GetPerformanceCounter ();
    if (!GetRandomNumber64 (&Samples[1]))
    {
      return EFI_DEVICE_ERROR;
    }
    Samples[2] = GetPerformanceCounter ();
    Samples[3] = 0xffe7070000ULL;
    // Timer RngLib is best-effort software randomness. Mix full counters
    // too, since its sampled low bytes wrap quickly. This is not entropy.
    if (!Sha256HashAll (Samples, sizeof (Samples), Digest))
    {
      return EFI_DEVICE_ERROR;
    }
    CopyMem (Base, Digest, sizeof (Base));
    Base[0] = (Base[0] & 0xfc) | 2;
    // Reserve an aligned pair: adding one cannot overflow the first octet
    // or change the local/unicast bits, even for an all-ones hash tail.
    Base[5] &= 0xfe;
    Ready = TRUE;
  }
  CopyMem (Mac, Base, sizeof (Base));
  Mac[5] += Index;
  return EFI_SUCCESS;
}
