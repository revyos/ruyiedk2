// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Protocol/BlockIo.h>

EFI_STATUS A210LocateBootAreas (EFI_BLOCK_IO_PROTOCOL *Boot[2]);

// User area, boot0 and boot1, in EXT_CSD partition-access order.
EFI_STATUS A210LocateEmmcAreas (EFI_BLOCK_IO_PROTOCOL *Areas[3]);
