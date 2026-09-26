/** @file
  Copyright (c) 2021, Hewlett Packard Enterprise Development LP.
  Copyright (c) 2025, Ventana Micro Systems Inc.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "PlatformSecLib.h"
#include <Library/PlatformResourceLib.h>

/**
  Perform Platform initialization.

  @param  FdtPointer      The pointer to the device tree.

  @return EFI_SUCCESS     The platform initialized successfully.
  @retval  Others        - As the error code indicates

**/
EFI_STATUS
EFIAPI
PlatformInitialization (
  VOID  *FdtPointer
  )
{
  VOID    *Base;
  VOID    *NewBase;
  UINTN   FdtSize;
  UINTN   FdtPages;
  UINT64  *FdtHobData;
  EFI_STATUS Status;

  SerialPortInitialize ();

  DEBUG ((DEBUG_INFO, "%a: Build FDT HOB - FDT at address: 0x%x \n", __func__, FdtPointer));
  Base = FdtPointer;
  if (FdtCheckHeader (Base) != 0) {
    DEBUG ((DEBUG_ERROR, "%a: Corrupted DTB\n", __func__));
    return EFI_UNSUPPORTED;
  }

  FdtSize  = FdtTotalSize (Base);
  FdtPages = EFI_SIZE_TO_PAGES (FdtSize);
  NewBase  = AllocatePages (FdtPages);
  if (NewBase == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Could not allocate memory for DTB\n", __func__));
    return EFI_UNSUPPORTED;
  }

  if (FdtOpenInto (Base, NewBase, EFI_PAGES_TO_SIZE (FdtPages)) != 0) {
    return EFI_DEVICE_ERROR;
  }

  FdtHobData = BuildGuidHob (&gFdtHobGuid, sizeof *FdtHobData);
  if (FdtHobData == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Could not build FDT Hob\n", __func__));
    return EFI_UNSUPPORTED;
  }

  *FdtHobData = (UINTN)NewBase;

  BuildMemoryAllocationHob (
    PcdGet32 (PcdOvmfDxeMemFvBase),
    PcdGet32 (PcdOvmfFirmwareFdSize),
    EfiBootServicesData
    );
  // Publish the FDT before hooks which depend on the timer frequency.
  Status = PlatformResourceInit (NewBase);
  return Status;
}
