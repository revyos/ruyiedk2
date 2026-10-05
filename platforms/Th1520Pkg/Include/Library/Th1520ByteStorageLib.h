// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

typedef EFI_STATUS (*TH1520_ALIGNED_STORAGE_IO)(
    UINT64 Offset, UINTN Length, VOID *Buffer, BOOLEAN Write);

// The backend transfers complete blocks; byte writes preserve adjacent data.
EFI_STATUS Th1520StorageBytes (
    UINT64 Capacity, UINTN BlockSize, TH1520_ALIGNED_STORAGE_IO Transfer,
    UINT64 Offset, UINTN Length, VOID *Buffer, BOOLEAN Write);
