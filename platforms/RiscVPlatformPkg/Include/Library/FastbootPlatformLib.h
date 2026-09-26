// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

#define FASTBOOT_DOWNLOAD_MAX SIZE_128MB
EFI_STATUS FastbootPlatformInit (VOID);
VOID FastbootPlatformStop (VOID);
EFI_STATUS FastbootGetVar (CONST CHAR8 *Name, CHAR8 *Value, UINTN Size);
EFI_STATUS FastbootFlash (CONST CHAR8 *Name, CONST VOID *Data, UINTN Size);
EFI_STATUS FastbootRead (CONST CHAR8 *Name, UINT64 Offset,
                        UINTN Size, VOID *Data);
