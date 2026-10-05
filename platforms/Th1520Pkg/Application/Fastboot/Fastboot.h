// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#define DOWNLOAD_MAX SIZE_32MB
EFI_STATUS UsbStart (VOID);
VOID UsbStop (VOID);
EFI_STATUS
UsbReceive (VOID *Buffer, UINTN *Length);
EFI_STATUS
UsbSend (CONST VOID *Buffer, UINTN Length);
EFI_STATUS
SpiFlash (UINT64 Offset, CONST VOID *Buffer, UINTN Length);
EFI_STATUS SpiRead (UINT64 Offset, VOID *Buffer, UINTN Length);
