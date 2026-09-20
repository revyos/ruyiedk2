// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
typedef struct _TH1520_BOOT_AREA_PROTOCOL TH1520_BOOT_AREA_PROTOCOL;
struct _TH1520_BOOT_AREA_PROTOCOL {
  UINT64 Size;
  EFI_STATUS (EFIAPI *Read)(TH1520_BOOT_AREA_PROTOCOL *This, UINTN Offset,
                           UINTN Length, VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(TH1520_BOOT_AREA_PROTOCOL *This,
                            CONST VOID *Buffer, UINTN Length);
};
extern EFI_GUID gTh1520BootAreaProtocolGuid;
