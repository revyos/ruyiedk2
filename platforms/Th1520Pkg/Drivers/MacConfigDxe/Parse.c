// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520MacStoreLib.h>

STATIC INTN
Hex (CHAR16 C)
{
  if (C >= L'0' && C <= L'9') return C - L'0';
  if (C >= L'a' && C <= L'f') return C - L'a' + 10;
  if (C >= L'A' && C <= L'F') return C - L'A' + 10;
  return -1;
}

EFI_STATUS
MacParse (CONST CHAR16 *Text, UINT8 *Mac)
{
  if (!Text || !Mac)
    return EFI_INVALID_PARAMETER;
  for (UINTN I = 0; I < 6; I++)
  {
    // Check each character before moving forward, including short input.
    INTN High = Hex (*Text++);
    if (High < 0) return EFI_INVALID_PARAMETER;
    INTN Low = Hex (*Text++);
    if (Low < 0) return EFI_INVALID_PARAMETER;
    Mac[I] = (UINT8)((High << 4) | Low);
    if (I != 5 && *Text++ != L':')
      return EFI_INVALID_PARAMETER;
  }
  return !*Text && Th1520MacValid (Mac) ?
      EFI_SUCCESS : EFI_INVALID_PARAMETER;
}
