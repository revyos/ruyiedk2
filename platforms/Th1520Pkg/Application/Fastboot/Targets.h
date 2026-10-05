// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef enum {
  Th1520TargetNone,
  Th1520TargetSpi,
  Th1520TargetEmmc,
  Th1520TargetEmmcBoot0,
  Th1520TargetEmmcBoot1
} TH1520_FLASH_TARGET;

typedef struct {
  TH1520_FLASH_TARGET Target;
  UINT64 Offset;
  UINT64 Length;
  BOOLEAN HasLength;
} TH1520_STORAGE_RANGE;

BOOLEAN Th1520ParseStorageRange (CONST CHAR8 *Name, TH1520_STORAGE_RANGE *Range);
UINT64 Th1520TargetCapacity (TH1520_FLASH_TARGET Target,
                           UINT64 UserSize, UINT64 BootSize);
