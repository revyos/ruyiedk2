// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
VOID Th1520Pad (UINTN Base, UINTN Pin, UINT32 Mux, UINT32 Config);
VOID Th1520GpioOutput (UINTN Base, UINTN Pin, BOOLEAN High);
EFI_STATUS Th1520Pca9557Output (
    UINTN I2c, UINT8 Address, UINT8 Mask, UINT8 Value);

EFI_STATUS Th1520FdtPinctrl (CONST VOID *Fdt, INT32 Node);
EFI_STATUS Th1520FdtEarlyInit (CONST VOID *Fdt);
EFI_STATUS Th1520FdtUsbHostInit (CONST VOID *Fdt, UINTN *Base);
