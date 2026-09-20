// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
typedef struct
{
  CONST CHAR8 *Configuration;
  CONST CHAR8 *Compatible;
  CONST CHAR8 *Medium;
  UINTN MaximumSize;
} TH1520_IMAGE_POLICY;
BOOLEAN
Th1520ValidImage (CONST UINT8 *Image, UINTN Size, CONST TH1520_IMAGE_POLICY *Policy);
