// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#define TH1520_SPI_RUNTIME_PAGES 7
EFI_STATUS Th1520SpiRuntimePrepare (VOID *Pages);
EFI_STATUS Th1520SpiInit (VOID);
EFI_STATUS Th1520SpiInitNative (VOID);
EFI_STATUS Th1520SpiRuntimeInit (VOID);
EFI_STATUS Th1520SpiConvertPointers (EFI_CONVERT_POINTER Convert);
EFI_STATUS Th1520SpiRead (UINTN Offset, UINTN Length, VOID *Buffer);
EFI_STATUS Th1520SpiErase (UINTN Offset, UINTN Length);
EFI_STATUS Th1520SpiProgram (
    UINTN Offset, UINTN Length, CONST VOID *Buffer);
