// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../platforms/Th1520Pkg/Application/Fastboot/Targets.h"
UINTN EFIAPI AsciiStrLen (CONST CHAR8 *S) { return strlen (S); }
INTN EFIAPI AsciiStrnCmp (CONST CHAR8 *A, CONST CHAR8 *B, UINTN N)
{ return strncmp (A, B, N); }
int main (void)
{
  TH1520_STORAGE_RANGE R;
  const char *Names[] = {"spi", "emmc0", "emmc0boot0", "emmc0boot1"};
  TH1520_FLASH_TARGET Targets[] = {Th1520TargetSpi, Th1520TargetEmmc,
      Th1520TargetEmmcBoot0, Th1520TargetEmmcBoot1};
  for (UINTN I = 0; I < ARRAY_SIZE (Names); I++) {
    assert (Th1520ParseStorageRange (Names[I], &R));
    assert (R.Target == Targets[I] && R.Offset == 0 && !R.HasLength);
  }
  assert (Th1520ParseStorageRange ("emmc0:100000001:0x123", &R));
  assert (R.Offset == 0x100000001ULL && R.Length == 0x123 && R.HasLength);
  assert (Th1520ParseStorageRange ("spi:FFFFFFFFFFFFFFFF", &R));
  assert (R.Offset == MAX_UINT64 && !R.HasLength);
  const char *Invalid[] = {"", "loader", "emmcboot0", "emmc0boot2", "SPI",
      "spi:", "spi:-1", "spi:+1", "spi: 1", "spi:0x", "spi:0:",
      "spi:1:2:3", "spi:10000000000000000", "spix", "emmc0boot0x"};
  for (UINTN I = 0; I < ARRAY_SIZE (Invalid); I++)
    assert (!Th1520ParseStorageRange (Invalid[I], &R));
  assert (!Th1520ParseStorageRange (NULL, &R));
  assert (!Th1520ParseStorageRange ("spi", NULL));
  assert (Th1520TargetCapacity (Th1520TargetSpi, 0, 0) == SIZE_8MB);
  assert (Th1520TargetCapacity (Th1520TargetEmmc, 0x1000000000ULL, SIZE_4MB) == 0x1000000000ULL);
  assert (Th1520TargetCapacity (Th1520TargetEmmcBoot0, 0, SIZE_4MB) == SIZE_4MB);
  assert (Th1520TargetCapacity (Th1520TargetEmmcBoot1, 0, SIZE_4MB) == SIZE_4MB);
  puts ("Media names, 64-bit offsets, capacities and malformed ranges passed");
}
