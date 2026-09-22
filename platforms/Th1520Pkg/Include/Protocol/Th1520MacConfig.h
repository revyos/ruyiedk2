// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef struct {
  UINT8 OverrideMask;
  UINT8 Reserved[3];
  UINT8 Address[2][6];
} TH1520_MAC_SETTINGS;

typedef enum {
  Th1520MacUnavailable,
  Th1520MacDeviceTree,
  Th1520MacConfigured,
  Th1520MacFactory,
  Th1520MacFallback
} TH1520_MAC_SOURCE;

typedef struct {
  BOOLEAN Present;
  UINT8 Address[6];
  TH1520_MAC_SOURCE Source;
} TH1520_MAC_INFO;

typedef struct {
  EFI_STATUS (EFIAPI *Get)(TH1520_MAC_SETTINGS *Settings,
                           TH1520_MAC_INFO Info[2]);
  EFI_STATUS (EFIAPI *Set)(CONST TH1520_MAC_SETTINGS *Settings);
} TH1520_MAC_CONFIG_PROTOCOL;
extern EFI_GUID gTh1520MacConfigProtocolGuid;
