// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Protocol/BlockIo.h>

EFI_STATUS A210LocateBootAreas (EFI_BLOCK_IO_PROTOCOL *Boot[2]);
