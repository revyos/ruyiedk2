// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#define EMMC_DMA_SIZE SIZE_512KB
#define EMMC_ADMA_COUNT (EMMC_DMA_SIZE / SIZE_64KB + 1)
typedef struct
{
  UINT16 Attributes;
  UINT16 Length;
  UINT32 Address;
} EMMC_ADMA_DESCRIPTOR;
EFI_STATUS
EmmcBuildAdma (EMMC_ADMA_DESCRIPTOR *Table, UINTN Capacity,
               UINT64 Address, UINTN Size);
