// SPDX-License-Identifier: BSD-2-Clause-Patent
// Software-only RNG for firmware network identifiers, not cryptographic keys.
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/RngLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Rng.h>

STATIC EFI_RNG_PROTOCOL Protocol;

STATIC EFI_STATUS EFIAPI
GetInfo (EFI_RNG_PROTOCOL *This, UINTN *Size, EFI_RNG_ALGORITHM *Algorithms)
{
  if (This != &Protocol || Size == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (*Size < sizeof (EFI_GUID))
  {
    *Size = sizeof (EFI_GUID);
    return EFI_BUFFER_TOO_SMALL;
  }
  if (Algorithms == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  CopyGuid (Algorithms, &gEdkiiRngAlgorithmUnSafe);
  *Size = sizeof (EFI_GUID);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
GetRng (EFI_RNG_PROTOCOL *This, EFI_RNG_ALGORITHM *Algorithm, UINTN Size, UINT8 *Buffer)
{
  UINT64 Value;
  UINTN Count;
  if (This != &Protocol || Size == 0 || Buffer == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Algorithm != NULL && !CompareGuid (Algorithm, &gEdkiiRngAlgorithmUnSafe))
  {
    return EFI_UNSUPPORTED;
  }
  while (Size != 0)
  {
    if (!GetRandomNumber64 (&Value))
    {
      return EFI_DEVICE_ERROR;
    }
    Count = MIN (Size, sizeof (Value));
    CopyMem (Buffer, &Value, Count);
    Buffer += Count;
    Size -= Count;
  }
  return EFI_SUCCESS;
}
STATIC EFI_RNG_PROTOCOL Protocol = {GetInfo, GetRng};

EFI_STATUS EFIAPI
SoftwareRngInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  return gBS->InstallProtocolInterface (&ImageHandle, &gEfiRngProtocolGuid, EFI_NATIVE_INTERFACE,
                                        &Protocol);
}
