/** @file
  Copyright (c) 2021, Hewlett Packard Enterprise Development LP. All rights reserved.<BR>
  Copyright (c) 2025, Ventana Micro Systems Inc. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformSecLib.h"
#include <Library/IoLib.h>
#include <Library/Th1520BoardSupportLib.h>

/**
  Build memory map I/O range resource HOB using the
  base address and size.

  @param  MemoryBase     Memory map I/O base.
  @param  MemorySize     Memory map I/O size.

**/
STATIC
VOID
AddIoMemoryBaseSizeHob (
  EFI_PHYSICAL_ADDRESS  MemoryBase,
  UINT64                MemorySize
  )
{
  /* Align to EFI_PAGE_SIZE */
  MemorySize = ALIGN_VALUE (MemorySize, EFI_PAGE_SIZE);
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    EFI_RESOURCE_ATTRIBUTE_PRESENT     |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    MemoryBase,
    MemorySize
    );
}

/**
  Populate IO resources from FDT that not added to GCD by its
  driver in the DXE phase.

  @param  FdtBase       Fdt base address
  @param  Compatible    Compatible string

**/
STATIC
VOID
PopulateIoResources (
  VOID         *FdtBase,
  CONST CHAR8  *Compatible
  )
{
  UINT64  *Reg;
  INT32   Node, LenP;

  Node = FdtNodeOffsetByCompatible (FdtBase, -1, Compatible);
  while (Node != -FDT_ERR_NOTFOUND) {
    Reg = (UINT64 *)FdtGetProp (FdtBase, Node, "reg", &LenP);
    if (Reg) {
      ASSERT (LenP == (2 * sizeof (UINT64)));
      AddIoMemoryBaseSizeHob (SwapBytes64 (Reg[0]), SwapBytes64 (Reg[1]));
    }

    Node = FdtNodeOffsetByCompatible (FdtBase, Node, Compatible);
  }
}

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

  FdtOpenInto (Base, NewBase, EFI_PAGES_TO_SIZE (FdtPages));

  FdtHobData = BuildGuidHob (&gFdtHobGuid, sizeof *FdtHobData);
  if (FdtHobData == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Could not build FDT Hob\n", __func__));
    return EFI_UNSUPPORTED;
  }

  *FdtHobData = (UINTN)NewBase;

  // TimerLib needs the FDT HOB before GPIO supply delays can run.
  EFI_STATUS Status = Th1520FdtEarlyInit (NewBase);
  if (EFI_ERROR (Status))
    DEBUG ((DEBUG_ERROR, "TH1520 board resources: %r\n", Status));

  PopulateIoResources (Base, "snps,dw-apb-uart");
  BuildMemoryAllocationHob (PcdGet32 (PcdOvmfDxeMemFvBase), SIZE_4MB, EfiBootServicesData);
  BuildMemoryAllocationHob (0, EFI_PAGE_SIZE, EfiReservedMemoryType);
  AddIoMemoryBaseSizeHob (0xffe7000000ULL, SIZE_1MB);
  AddIoMemoryBaseSizeHob (0xffea000000ULL, SIZE_4KB);
  AddIoMemoryBaseSizeHob (0xffec000000ULL, SIZE_1MB);
  AddIoMemoryBaseSizeHob (0xffef000000ULL, SIZE_1MB);
  AddIoMemoryBaseSizeHob (0xffef528000ULL, SIZE_4KB);
  AddIoMemoryBaseSizeHob (0xffef540000ULL, SIZE_256KB);
  AddIoMemoryBaseSizeHob (0xffef600000ULL, SIZE_1MB);
  AddIoMemoryBaseSizeHob (0xffefc30000ULL, SIZE_4KB);
  AddIoMemoryBaseSizeHob (0xfffff40000ULL, SIZE_64KB);
  AddIoMemoryBaseSizeHob (0xffffc39000ULL, SIZE_4KB);
  AddIoMemoryBaseSizeHob (0xffffc44000ULL, SIZE_4KB);
  AddIoMemoryBaseSizeHob (0xfffc000000ULL, SIZE_1MB);

  return EFI_SUCCESS;
}
