// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Protocol/BlockIo.h>

typedef EFI_STATUS (*FASTBOOT_STORAGE_PROGRESS) (UINT64 Done,
                                                 UINT64 Total,
                                                 BOOLEAN Verify);

// Sparse offsets are relative to the whole selected block device.
// DONT_CARE preserves data, including data from earlier downloads.
EFI_STATUS FastbootWriteImage (EFI_BLOCK_IO_PROTOCOL *Io,
                              CONST VOID *Data, UINTN Size,
                              BOOLEAN AllowSparse,
                              FASTBOOT_STORAGE_PROGRESS Progress);

typedef struct
{
  CHAR8 Name[32];
  EFI_BLOCK_IO_PROTOCOL *Io;
  UINTN Segment, Bus, Device, Function;
  UINT32 Namespace;
} FASTBOOT_NVME_TARGET;

EFI_STATUS FastbootLocateNvme (FASTBOOT_NVME_TARGET **Targets,
                              UINTN *Count);
