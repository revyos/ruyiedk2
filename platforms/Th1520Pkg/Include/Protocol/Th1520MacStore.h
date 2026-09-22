// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Protocol/Th1520MacConfig.h>
typedef struct _TH1520_MAC_STORE_PROTOCOL TH1520_MAC_STORE_PROTOCOL;
struct _TH1520_MAC_STORE_PROTOCOL {
  EFI_STATUS (EFIAPI *Read)(UINT8 *Mac);
  EFI_STATUS (EFIAPI *Write)(CONST UINT8 *Mac);
  EFI_STATUS (EFIAPI *GetSettings)(TH1520_MAC_SETTINGS *Settings);
  EFI_STATUS (EFIAPI *SetSettings)(CONST TH1520_MAC_SETTINGS *Settings);
};
extern EFI_GUID gTh1520MacStoreProtocolGuid;
extern EFI_GUID gTh1520NvStoreReadyProtocolGuid;
extern EFI_GUID gTh1520NvStoreGuid;
