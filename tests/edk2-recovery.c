// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../platforms/Th1520Pkg/Application/Fastboot/Targets.h"

INTN EFIAPI AsciiStrCmp (CONST CHAR8 *Left, CONST CHAR8 *Right)
{
  return strcmp (Left, Right);
}

UINT32 EFIAPI ReadUnaligned32 (CONST VOID *Buffer)
{
  UINT32 Value;
  memcpy (&Value, Buffer, sizeof (Value));
  return Value;
}

int main (void)
{
  UINT8 Data[9] = {0};
  UINT32 Sparse = 0xed26ff3aU;
  assert (Th1520RecoveryTarget (NULL) == Th1520TargetNone);
  assert (Th1520RecoveryTarget ("spi") == Th1520TargetSpi);
  assert (Th1520RecoveryTarget ("emmcboot0") == Th1520TargetEmmcBoot0);
  assert (Th1520RecoveryTarget ("emmcboot1") == Th1520TargetEmmcBoot1);
  CONST CHAR8 *Invalid[] = {"", "emmc", "emmcboot2", "emmcboot0:1",
                            "userdata", "loader", "SPI", "spi0"};
  for (UINTN I = 0; I < sizeof (Invalid) / sizeof (Invalid[0]); I++) {
    assert (Th1520RecoveryTarget (Invalid[I]) == Th1520TargetNone);
  }
  assert (Th1520TargetMaximum (Th1520TargetSpi, 0) == SIZE_8MB);
  assert (Th1520TargetMaximum (Th1520TargetEmmcBoot0, 0) == 0);
  assert (Th1520TargetMaximum (Th1520TargetEmmcBoot0, SIZE_4MB) == SIZE_4MB);
  assert (Th1520TargetMaximum (Th1520TargetEmmcBoot1, SIZE_4MB) == SIZE_4MB);
  assert (Th1520TargetMaximum (Th1520TargetNone, SIZE_4MB) == 0);
  assert (!Th1520ValidRawImage (NULL, 1, SIZE_4MB));
  assert (!Th1520ValidRawImage (Data, 0, SIZE_4MB));
  assert (!Th1520ValidRawImage (Data, sizeof (Data), 0));
  assert (!Th1520ValidRawImage (Data, SIZE_4MB + 1, SIZE_4MB));
  assert (Th1520ValidRawImage (Data, sizeof (Data), sizeof (Data)));
  assert (Th1520ValidRawImage (Data + 1, 1, 1));
  memcpy (Data + 1, &Sparse, sizeof (Sparse));
  assert (!Th1520ValidRawImage (Data + 1, sizeof (Sparse), SIZE_4MB));
  puts ("Recovery target names, bounds and sparse rejection passed");
  return 0;
}
