// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
VOID Th1520Pad (UINTN Base, UINTN Pin, UINT32 Mux, UINT32 Config);
VOID Th1520GpioOutput (UINTN Base, UINTN Pin, BOOLEAN High);
VOID Th1520Fan (UINTN Pin, UINTN Mux, UINTN Channel);
EFI_STATUS Th1520Pca9557Output (
    UINTN I2c, UINT8 Address, UINT8 Mask, UINT8 Value);

EFI_STATUS Th1520FdtPinctrl (CONST VOID *Fdt, INT32 Node);
