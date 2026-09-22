// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
UINT32 Th1520SpiMmioRead32 (UINTN Address);
UINT32 Th1520SpiMmioWrite32 (UINTN Address, UINT32 Value);
EFI_STATUS Th1520SpiMapRuntimeIo (
    EFI_CONVERT_POINTER Convert, UINTN Spi, UINTN Ap);
