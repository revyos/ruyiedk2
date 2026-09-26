// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef struct {
  CHAR8 Name[80];
  CHAR8 Manufacturer[32];
  UINT16 Cores;
  UINT16 EnabledCores;
  UINT16 CurrentMhz;
} PLATFORM_PROCESSOR_INFO;

EFI_STATUS PlatformGetProcessorInfo (CONST VOID *Fdt,
                                    PLATFORM_PROCESSOR_INFO *Info);
