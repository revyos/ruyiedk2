// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef struct _TH1520_EMMC_ACCESS_PROTOCOL TH1520_EMMC_ACCESS_PROTOCOL;
struct _TH1520_EMMC_ACCESS_PROTOCOL {
  UINT64 UserSize;
  UINT64 BootSize;
  // Partition 0 is the user area; 1 and 2 are hardware boot0 and boot1.
  EFI_STATUS (EFIAPI *Read)(TH1520_EMMC_ACCESS_PROTOCOL *This,
      UINTN Partition, UINT64 Offset, UINTN Length, VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(TH1520_EMMC_ACCESS_PROTOCOL *This,
      UINTN Partition, UINT64 Offset, UINTN Length, CONST VOID *Buffer);
};
extern EFI_GUID gTh1520EmmcAccessProtocolGuid;
