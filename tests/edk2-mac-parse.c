// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/Th1520MacStoreLib.h>
#include <Library/BaseMemoryLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
EFI_STATUS MacParse (CONST CHAR16 *Text, UINT8 *Mac);
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
int main (void)
{
  UINT8 Mac[6];
  const CHAR16 *Invalid[] = {
    L"", L"0", L"00", L"00:11:22:33:44", L"00:11:22:33:44:5",
    L"00-11-22-33-44-55", L"00:11:22:33:44:GG", L"000:11:22:33:44:55",
    L"00:11:22:33:44:55 ", L"00:00:00:00:00:00", L"ff:ff:ff:ff:ff:ff",
    L"01:00:5e:00:00:01", L"33:33:00:00:00:01"
  };
  assert (MacParse (L"02:aB:CD:ef:00:01", Mac) == EFI_SUCCESS);
  assert (Mac[0] == 2 && Mac[1] == 0xab && Mac[5] == 1);
  assert (MacParse (L"00:11:22:33:44:55", Mac) == EFI_SUCCESS);
  for (UINTN I = 0; I < sizeof (Invalid) / sizeof (*Invalid); I++)
    assert (MacParse (Invalid[I], Mac) == EFI_INVALID_PARAMETER);
  assert (MacParse (NULL, Mac) == EFI_INVALID_PARAMETER);
  puts ("BIOS MAC input: syntax and unicast validation passed");
}
