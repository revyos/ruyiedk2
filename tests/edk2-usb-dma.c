// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exercise the firmware DMA adapter through its PCI I/O callbacks.
#define _GNU_SOURCE
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "../platforms/Th1520Pkg/Drivers/UsbHostDxe/Dma.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

static void *Pool;
static size_t PoolSize;
static EFI_TPL CurrentTpl = TPL_APPLICATION;
static unsigned CleanCalls, InvalidateCalls;
static size_t CleanBytes;
static EFI_TPL EFIAPI
Raise (EFI_TPL Tpl)
{
  EFI_TPL Old = CurrentTpl;
  assert (Tpl >= Old && Tpl == TPL_NOTIFY);
  CurrentTpl = Tpl;
  return Old;
}
static void EFIAPI
Restore (EFI_TPL Tpl)
{
  assert (Tpl <= CurrentTpl);
  CurrentTpl = Tpl;
}
static EFI_STATUS EFIAPI
Pages (EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Count,
       EFI_PHYSICAL_ADDRESS *Address)
{
  assert (Type == AllocateMaxAddress && *Address == MAX_UINT32);
  assert (MemoryType == EfiBootServicesData && !Pool);
  PoolSize = EFI_PAGES_TO_SIZE (Count);
  Pool =
      mmap (NULL, PoolSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  assert (Pool != MAP_FAILED && (UINTN)Pool + PoolSize - 1 <= MAX_UINT32);
  *Address = (UINTN)Pool;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
TestFreePages (EFI_PHYSICAL_ADDRESS Address, UINTN Count)
{
  assert ((void *)(UINTN)Address == Pool && EFI_PAGES_TO_SIZE (Count) == PoolSize);
  assert (!munmap (Pool, PoolSize));
  Pool = NULL;
  return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES Services = {
    .RaiseTPL = Raise, .RestoreTPL = Restore, .AllocatePages = Pages, .FreePages = TestFreePages};
EFI_BOOT_SERVICES *gBS = &Services;
VOID *EFIAPI
CopyMem (VOID *D, CONST VOID *S, UINTN N)
{
  return memcpy (D, S, N);
}
VOID *EFIAPI
SetMem (VOID *D, UINTN N, UINT8 V)
{
  return memset (D, V, N);
}
VOID *EFIAPI
ZeroMem (VOID *D, UINTN N)
{
  return memset (D, 0, N);
}
VOID *EFIAPI
AllocateZeroPool (UINTN N)
{
  return calloc (1, N);
}
VOID EFIAPI
FreePool (VOID *P)
{
  free (P);
}
VOID EFIAPI
MemoryFence (VOID)
{
  __atomic_thread_fence (__ATOMIC_SEQ_CST);
}
static void
CheckRange (void *P, size_t N)
{
  assert (Pool && (UINTN)P >= (UINTN)Pool && N);
  assert ((UINTN)P - (UINTN)Pool + N <= PoolSize);
  assert (!((UINTN)P & EFI_PAGE_MASK) && !(N & EFI_PAGE_MASK));
}
VOID *EFIAPI
WriteBackInvalidateDataCacheRange (VOID *P, UINTN N)
{
  CheckRange (P, N);
  CleanCalls++;
  CleanBytes += N;
  return P;
}
VOID *EFIAPI
InvalidateDataCacheRange (VOID *P, UINTN N)
{
  CheckRange (P, N);
  InvalidateCalls++;
  return P;
}
int
main (void)
{
  EFI_PCI_IO_PROTOCOL Pci = {0};
  VOID *Common, *Extra, *Mapping;
  EFI_PHYSICAL_ADDRESS Device;
  UINTN Bytes;
  unsigned char Caller[5002];
  assert (UsbDmaInitialize () == EFI_SUCCESS);
  assert (UsbDmaBind (&Pci) == EFI_SUCCESS && UsbDmaIdle ());
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 0, &Common, 0) ==
          EFI_INVALID_PARAMETER);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 257, &Common, 0) ==
          EFI_OUT_OF_RESOURCES);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiRuntimeServicesData, 1, &Common, 0) ==
          EFI_UNSUPPORTED);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 1, &Common,
                              EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE) == EFI_UNSUPPORTED);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 20, &Common, 0) ==
          EFI_SUCCESS);
  // Aligned allocations may release leading/trailing subranges.
  assert (Pci.FreeBuffer (&Pci, 1, Common) == EFI_SUCCESS);
  assert (Pci.FreeBuffer (&Pci, 3, (char *)Common + 17 * EFI_PAGE_SIZE) == EFI_SUCCESS);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 1, &Extra, 0) ==
              EFI_SUCCESS &&
          Extra == Common);
  assert (Pci.FreeBuffer (&Pci, 1, Extra) == EFI_SUCCESS);
  assert (Pci.FreeBuffer (&Pci, 16, (char *)Common + EFI_PAGE_SIZE) == EFI_SUCCESS);
  assert (Pci.FreeBuffer (&Pci, 1, Common) == EFI_INVALID_PARAMETER && UsbDmaIdle ());
  Bytes = 1;
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterCommonBuffer, Common, &Bytes, &Device,
                   &Mapping) == EFI_UNSUPPORTED);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 256, &Common, 0) ==
          EFI_SUCCESS);
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 1, &Extra, 0) ==
          EFI_OUT_OF_RESOURCES);
  Bytes = sizeof (Caller);
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterRead, Caller, &Bytes, &Device, &Mapping) ==
          EFI_OUT_OF_RESOURCES);
  Bytes = PoolSize;
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterCommonBuffer, Common, &Bytes, &Device,
                   &Mapping) == EFI_SUCCESS &&
          !Mapping && Device == (UINTN)Common);
  Bytes++;
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterCommonBuffer, Common, &Bytes, &Device,
                   &Mapping) == EFI_UNSUPPORTED);
  UsbDmaRelease ();
  assert (Pool); // Never release a pool that the controller still owns.
  assert (Pci.FreeBuffer (&Pci, 256, Common) == EFI_SUCCESS && UsbDmaIdle ());
  assert (Pci.AllocateBuffer (&Pci, AllocateAnyPages, EfiBootServicesData, 3, &Common, 0) ==
          EFI_SUCCESS);
  memset (Caller, 0xa5, sizeof (Caller));
  Bytes = sizeof (Caller) - 2;
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterWrite, Caller + 1, &Bytes, &Device, &Mapping) ==
          EFI_SUCCESS);
  assert (Device != (UINTN)(Caller + 1) && !(Device & EFI_PAGE_MASK));
  memset ((void *)(UINTN)Device, 0x5a, 111); // Simulate a short input DMA.
  CleanCalls = 0;
  CleanBytes = 0;
  UsbDmaSync ();
  assert (CleanCalls == 1 && CleanBytes == 3 * EFI_PAGE_SIZE); // Excludes streaming pages.
  assert (Pci.Unmap (&Pci, Mapping) == EFI_SUCCESS && InvalidateCalls == 1);
  for (size_t I = 0; I < sizeof (Caller); I++)
  {
    assert (Caller[I] == (I >= 1 && I <= 111 ? 0x5a : 0xa5));
  }
  Bytes = sizeof (Caller) - 2;
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterRead, Caller + 1, &Bytes, &Device, &Mapping) ==
          EFI_SUCCESS);
  assert (!memcmp ((void *)(UINTN)Device, Caller + 1, Bytes));
  memset ((void *)(UINTN)Device, 0, Bytes);
  assert (Pci.Unmap (&Pci, Mapping) == EFI_SUCCESS && Caller[1] == 0x5a && InvalidateCalls == 1);
  assert (Pci.Map (&Pci, (EFI_PCI_IO_PROTOCOL_OPERATION)-1, Caller, &Bytes, &Device, &Mapping) ==
          EFI_INVALID_PARAMETER);
  assert (Pci.Map (&Pci, EfiPciIoOperationBusMasterRead, (VOID *)(MAX_UINTN - 1), &Bytes, &Device,
                   &Mapping) == EFI_INVALID_PARAMETER);
  assert (Pci.FreeBuffer (&Pci, 1, (char *)Common + 1) == EFI_INVALID_PARAMETER);
  assert (Pci.FreeBuffer (&Pci, 1, (char *)Common + EFI_PAGE_SIZE) == EFI_SUCCESS);
  CleanCalls = 0;
  CleanBytes = 0;
  assert (Pci.Flush (&Pci) == EFI_SUCCESS && CleanCalls == 2 && CleanBytes == 2 * EFI_PAGE_SIZE);
  assert (Pci.FreeBuffer (&Pci, 1, Common) == EFI_SUCCESS);
  assert (Pci.FreeBuffer (&Pci, 1, (char *)Common + 2 * EFI_PAGE_SIZE) == EFI_SUCCESS &&
          UsbDmaIdle ());
  UsbDmaRelease ();
  assert (!Pool && CurrentTpl == TPL_APPLICATION);
  puts ("USB DMA: allocation, mapping, cache ranges and short transfers passed");
  return 0;
}
