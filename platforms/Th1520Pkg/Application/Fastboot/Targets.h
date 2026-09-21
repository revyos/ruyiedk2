// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef enum {
  Th1520TargetNone,
  Th1520TargetSpi,
  Th1520TargetEmmcBoot0,
  Th1520TargetEmmcBoot1
} TH1520_FLASH_TARGET;

TH1520_FLASH_TARGET Th1520RecoveryTarget (CONST CHAR8 *Name);
UINTN Th1520TargetMaximum (TH1520_FLASH_TARGET Target, UINT64 BootAreaSize);
BOOLEAN Th1520ValidRawImage (CONST VOID *Buffer, UINTN Length, UINTN Maximum);
