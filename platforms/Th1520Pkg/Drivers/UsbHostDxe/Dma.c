// SPDX-License-Identifier: BSD-2-Clause-Patent
// DMA adapter for the pinned EDK2 xHCI driver on a non-coherent C910.
#include "Dma.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define DMA_PAGES 256
#define PAGE_COMMON 1
#define PAGE_STREAM 2
#define MAP_SIGNATURE SIGNATURE_32 ('U', 'D', 'M', 'A')
STATIC EFI_PHYSICAL_ADDRESS DmaBase;
STATIC UINT8 Used[DMA_PAGES];
STATIC EFI_PCI_IO_PROTOCOL *Controller;

typedef struct
{
  UINT32 Signature;
  VOID *HostAddress;
  VOID *Bounce;
  UINTN Bytes, Pages;
  BOOLEAN DeviceWrites;
} USB_MAPPING;

STATIC EFI_STATUS
Allocate (UINTN Pages, UINT8 Kind, VOID **Address)
{
  UINTN Start, Index;
  EFI_TPL OldTpl;
  if (!Pages || !Address)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!DmaBase || Pages > DMA_PAGES)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (Start = 0; Start <= DMA_PAGES - Pages; Start++)
  {
    for (Index = 0; Index < Pages && !Used[Start + Index]; Index++)
    {
    }
    if (Index != Pages)
    {
      Start += Index;
      continue;
    }
    SetMem (Used + Start, Pages, Kind);
    *Address = (VOID *)(UINTN)(DmaBase + EFI_PAGES_TO_SIZE (Start));
    ZeroMem (*Address, EFI_PAGES_TO_SIZE (Pages));
    gBS->RestoreTPL (OldTpl);
    return EFI_SUCCESS;
  }
  gBS->RestoreTPL (OldTpl);
  return EFI_OUT_OF_RESOURCES;
}
STATIC EFI_STATUS
Release (UINTN Pages, VOID *Address, UINT8 Kind)
{
  UINTN Start, Index, Base = (UINTN)Address;
  EFI_TPL OldTpl;
  if (Base < DmaBase || (Base & EFI_PAGE_MASK) || !Pages || Pages > DMA_PAGES)
  {
    return EFI_INVALID_PARAMETER;
  }
  Start = (Base - DmaBase) >> EFI_PAGE_SHIFT;
  if (Start > DMA_PAGES - Pages)
  {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (Index = 0; Index < Pages; Index++)
  {
    if (Used[Start + Index] != Kind)
    {
      gBS->RestoreTPL (OldTpl);
      return EFI_INVALID_PARAMETER;
    }
  }
  ZeroMem (Used + Start, Pages);
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
AllocateBuffer (EFI_PCI_IO_PROTOCOL *This, EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
                UINTN Pages, VOID **Address, UINT64 Attributes)
{
  if (This != Controller || !Address)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (MemoryType != EfiBootServicesData || (Attributes & ~EFI_PCI_IO_ATTRIBUTE_MEMORY_CACHED))
  {
    return EFI_UNSUPPORTED;
  }
  return Allocate (Pages, PAGE_COMMON, Address);
}
STATIC EFI_STATUS EFIAPI
FreeBuffer (EFI_PCI_IO_PROTOCOL *This, UINTN Pages, VOID *Address)
{
  if (This != Controller)
  {
    return EFI_INVALID_PARAMETER;
  }
  return Release (Pages, Address, PAGE_COMMON);
}
STATIC EFI_STATUS EFIAPI
MapBuffer (EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_OPERATION Operation, VOID *Address,
           UINTN *Bytes, EFI_PHYSICAL_ADDRESS *DeviceAddress, VOID **Mapping)
{
  USB_MAPPING *Map;
  EFI_STATUS Status;
  UINTN Base = (UINTN)Address, First, Last, Index;
  EFI_TPL OldTpl;
  if (This != Controller || !Address || !Bytes || !*Bytes || !DeviceAddress || !Mapping ||
      (UINTN)Operation >= EfiPciIoOperationMaximum || *Bytes > MAX_UINTN - Base)
  {
    return EFI_INVALID_PARAMETER;
  }
  *Mapping = NULL;
  if (Operation == EfiPciIoOperationBusMasterCommonBuffer)
  {
    if (Base < DmaBase || Base >= DmaBase + EFI_PAGES_TO_SIZE (DMA_PAGES) ||
        *Bytes > DmaBase + EFI_PAGES_TO_SIZE (DMA_PAGES) - Base)
    {
      return EFI_UNSUPPORTED;
    }
    First = (Base - DmaBase) >> EFI_PAGE_SHIFT;
    Last = (Base - DmaBase + *Bytes - 1) >> EFI_PAGE_SHIFT;
    OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    for (Index = First; Index <= Last; Index++)
    {
      if (Used[Index] != PAGE_COMMON)
      {
        gBS->RestoreTPL (OldTpl);
        return EFI_UNSUPPORTED;
      }
    }
    gBS->RestoreTPL (OldTpl);
    *DeviceAddress = Base;
    MemoryFence ();
    return EFI_SUCCESS;
  }
  if (*Bytes > EFI_PAGES_TO_SIZE (DMA_PAGES))
  {
    return EFI_OUT_OF_RESOURCES;
  }
  Map = AllocateZeroPool (sizeof (*Map));
  if (!Map)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  Map->Pages = EFI_SIZE_TO_PAGES (*Bytes);
  Status = Allocate (Map->Pages, PAGE_STREAM, &Map->Bounce);
  if (EFI_ERROR (Status))
  {
    FreePool (Map);
    return Status;
  }
  Map->Signature = MAP_SIGNATURE;
  Map->HostAddress = Address;
  Map->Bytes = *Bytes;
  Map->DeviceWrites = Operation == EfiPciIoOperationBusMasterWrite;
  // Preserve bytes beyond a short input transfer. Dedicated bounce pages
  // keep cache maintenance away from unrelated caller-owned cache lines.
  CopyMem (Map->Bounce, Address, *Bytes);
  WriteBackInvalidateDataCacheRange (Map->Bounce, EFI_PAGES_TO_SIZE (Map->Pages));
  *DeviceAddress = (UINTN)Map->Bounce;
  *Mapping = Map;
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
UnmapBuffer (EFI_PCI_IO_PROTOCOL *This, VOID *Mapping)
{
  USB_MAPPING *Map = Mapping;
  EFI_STATUS Status;
  if (This != Controller)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!Map)
  {
    return EFI_SUCCESS;
  }
  if (Map->Signature != MAP_SIGNATURE)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Map->DeviceWrites)
  {
    InvalidateDataCacheRange (Map->Bounce, EFI_PAGES_TO_SIZE (Map->Pages));
    CopyMem (Map->HostAddress, Map->Bounce, Map->Bytes);
  }
  Status = Release (Map->Pages, Map->Bounce, PAGE_STREAM);
  Map->Signature = 0;
  FreePool (Map);
  return Status;
}
VOID
UsbDmaSync (VOID)
{
  UINTN Start, End;
  EFI_TPL OldTpl;
  if (!DmaBase)
  {
    return;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  // XhciDxe's UsbHcMem allocator uses 64-byte units, so CPU-written
  // command/input objects do not share cache lines with device-written
  // event/output objects. Clean and invalidate common pages before MMIO
  // publication/polling, including ERDP reads after consuming an event.
  // Streaming data is synchronized separately by Map/Unmap.
  for (Start = 0; Start < DMA_PAGES; Start = End)
  {
    End = Start + 1;
    if (Used[Start] != PAGE_COMMON)
    {
      continue;
    }
    while (End < DMA_PAGES && Used[End] == PAGE_COMMON)
    {
      End++;
    }
    WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)(DmaBase + EFI_PAGES_TO_SIZE (Start)),
                                       EFI_PAGES_TO_SIZE (End - Start));
  }
  MemoryFence ();
  gBS->RestoreTPL (OldTpl);
}
STATIC EFI_STATUS EFIAPI
Flush (EFI_PCI_IO_PROTOCOL *This)
{
  if (This != Controller)
  {
    return EFI_INVALID_PARAMETER;
  }
  UsbDmaSync ();
  return EFI_SUCCESS;
}
BOOLEAN
UsbDmaIdle (VOID)
{
  UINTN Index;
  for (Index = 0; Index < DMA_PAGES; Index++)
  {
    if (Used[Index])
    {
      return FALSE;
    }
  }
  return TRUE;
}
EFI_STATUS
UsbDmaInitialize (VOID)
{
  EFI_STATUS Status;
  if (DmaBase)
  {
    return EFI_ALREADY_STARTED;
  }
  DmaBase = MAX_UINT32;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, DMA_PAGES, &DmaBase);
  if (EFI_ERROR (Status))
  {
    DmaBase = 0;
  }
  return Status;
}
VOID
UsbDmaRelease (VOID)
{
  if (DmaBase && UsbDmaIdle ())
  {
    gBS->FreePages (DmaBase, DMA_PAGES);
    DmaBase = 0;
    Controller = NULL;
  }
}
EFI_STATUS
UsbDmaBind (EFI_PCI_IO_PROTOCOL *Pci)
{
  if (!Pci || !DmaBase)
  {
    return EFI_INVALID_PARAMETER;
  }
  Controller = Pci;
  Pci->AllocateBuffer = AllocateBuffer;
  Pci->FreeBuffer = FreeBuffer;
  Pci->Map = MapBuffer;
  Pci->Unmap = UnmapBuffer;
  Pci->Flush = Flush;
  return EFI_SUCCESS;
}
