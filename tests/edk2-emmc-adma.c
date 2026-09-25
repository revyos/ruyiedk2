// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/Th1520Pkg/Drivers/EmmcDxe/Adma.h"
#include <assert.h>
#include <stdio.h>

static void verify(UINT64 address, UINTN size)
{
  EMMC_ADMA_DESCRIPTOR table[EMMC_ADMA_COUNT];
  UINTN count = 0, total = 0;
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, address, size) == 0);
  do {
    UINTN length = table[count].Length ? table[count].Length : 65536;
    assert(table[count].Attributes & BIT0);
    assert(table[count].Attributes & BIT5);
    assert(table[count].Address == address + total);
    assert((table[count].Address / SIZE_128MB) ==
           ((table[count].Address + length - 1) / SIZE_128MB));
    total += length;
    assert(total <= size);
  } while (!(table[count++].Attributes & BIT1));
  assert(total == size && count <= EMMC_ADMA_COUNT);
}

int main(void)
{
  EMMC_ADMA_DESCRIPTOR table[EMMC_ADMA_COUNT];
  verify(0x100000, 512);
  verify(0x100000, 65536);
  verify(0x100000, EMMC_DMA_SIZE);
  verify(SIZE_128MB - 512, EMMC_DMA_SIZE);
  verify(0x100000000ULL - EMMC_DMA_SIZE, EMMC_DMA_SIZE);
  assert(EmmcBuildAdma(table, 0, 0x1000, 512) == EFI_BUFFER_TOO_SMALL);
  assert(EmmcBuildAdma(table, 1, SIZE_128MB - 512, 1024) ==
         EFI_BUFFER_TOO_SMALL);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0xffffffff, 512) ==
         EFI_INVALID_PARAMETER);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0xffffff00, 512) ==
         EFI_INVALID_PARAMETER);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0x100000000ULL, 512) ==
         EFI_INVALID_PARAMETER);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0x1001, 512) ==
         EFI_INVALID_PARAMETER);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0x1000, 0) ==
         EFI_INVALID_PARAMETER);
  assert(EmmcBuildAdma(table, EMMC_ADMA_COUNT, 0x1000,
                       EMMC_DMA_SIZE + 512) == EFI_INVALID_PARAMETER);
  puts("ADMA2 length, 128 MiB boundary and 32-bit range checks passed");
}
