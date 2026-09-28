// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Library/A210PeripheralLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Cpu.h>

STATIC EFI_CPU_SET_MEMORY_ATTRIBUTES mSetAttributes;
STATIC EFI_GCD_MEMORY_SPACE_DESCRIPTOR *mMemoryMap;
STATIC UINTN mMemoryCount;

STATIC EFI_STATUS EFIAPI
SetMemoryAttributes (EFI_CPU_ARCH_PROTOCOL *Cpu,
                     EFI_PHYSICAL_ADDRESS Base, UINT64 Length,
                     UINT64 Attributes)
{
  EFI_STATUS Status;
  if (!Length || Base > MAX_UINT64 - Length)
    return EFI_INVALID_PARAMETER;
  if (Attributes & (EFI_MEMORY_UC | EFI_MEMORY_WC))
  {
    // Clean the old cacheable mapping before switching its PBMT. Cache
    // operations after remapping must not be relied on to remove aliases.
    for (UINTN Index = 0; Index < mMemoryCount; Index++)
    {
      UINT64 Start, End;
      if (mMemoryMap[Index].GcdMemoryType != EfiGcdMemoryTypeSystemMemory)
        continue;
      Start = MAX (Base, mMemoryMap[Index].BaseAddress);
      End = MIN (Base + Length, mMemoryMap[Index].BaseAddress
                                 + mMemoryMap[Index].Length);
      if (Start >= End)
        continue;
      Status = Cpu->FlushDataCache (Cpu, Start, End - Start,
                                    EfiCpuFlushTypeWriteBackInvalidate);
      if (EFI_ERROR (Status))
        return Status;
    }
  }
  MemoryFence ();
  Status = mSetAttributes (Cpu, Base, Length, Attributes);
  MemoryFence ();
  return Status;
}

EFI_STATUS EFIAPI
CpuDmaInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_CPU_ARCH_PROTOCOL *Cpu;
  CONST VOID *Fdt = A210GetFdt ();
  INT32 Node = -1, Length;
  UINT32 Alignment = 64;
  EFI_STATUS Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Map;
  UINTN Count, Index;
  if (!Fdt || FdtNodeOffsetByCompatible (Fdt, -1, "zhihe,a210") != 0)
    return EFI_UNSUPPORTED;
  while ((Node = FdtNodeOffsetByPropValue (
      Fdt, Node, "device_type", "cpu", 4)) >= 0)
  {
    CONST UINT32 *Data;
    UINT32 Size;
    Data = FdtGetProp (Fdt, Node, "riscv,cbom-block-size", &Length);
    if (!Data || Length != 4)
      continue;
    Size = A210Cell (Data);
    if (!Size || (Size & (Size - 1)) || Size > EFI_PAGE_SIZE)
      return EFI_UNSUPPORTED;
    Alignment = MAX (Alignment, Size);
  }
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL,
                                (VOID **)&Cpu);
  if (EFI_ERROR (Status))
    return Status;
  // Run immediately after CpuDxe, before any DMA consumer is dispatched.
  // The cache library operates on 64-byte blocks. Streaming DMA must not
  // invalidate dirty pool metadata sharing a partially covered block.
  Cpu->DmaBufferAlignment = MAX (Cpu->DmaBufferAlignment, Alignment);
  DEBUG ((DEBUG_INFO, "A210 DMA buffer alignment: %u bytes\n",
           Cpu->DmaBufferAlignment));
  // CpuDxe builds the initial RAM/MMIO page mappings. Publish their
  // cache types in GCD too: DMA libraries use GCD rather than walking
  // the CPU page tables to decide whether a buffer is cacheable.
  Status = gDS->GetMemorySpaceMap (&Count, &Map);
  if (EFI_ERROR (Status))
    return Status;
  // Keep the RAM boundaries for the lifetime of this boot-service policy.
  // The callback must not query GCD while GCD holds its attribute lock.
  mMemoryMap = Map;
  mMemoryCount = Count;
  mSetAttributes = Cpu->SetMemoryAttributes;
  Cpu->SetMemoryAttributes = SetMemoryAttributes;
  for (Index = 0; Index < Count; Index++)
  {
    UINT64 Cache;
    if (Map[Index].Attributes & EFI_CACHE_ATTRIBUTE_MASK)
      continue;
    if (Map[Index].GcdMemoryType == EfiGcdMemoryTypeSystemMemory)
      Cache = EFI_MEMORY_WB;
    else if (Map[Index].GcdMemoryType == EfiGcdMemoryTypeMemoryMappedIo)
      Cache = EFI_MEMORY_UC;
    else
      continue;
    if (!(Map[Index].Capabilities & Cache))
    {
      Status = EFI_UNSUPPORTED;
      break;
    }
    Status = gDS->SetMemorySpaceAttributes (
        Map[Index].BaseAddress, Map[Index].Length,
        Map[Index].Attributes | Cache);
    if (EFI_ERROR (Status))
      break;
  }
  if (EFI_ERROR (Status))
  {
    DEBUG ((DEBUG_ERROR, "A210 DMA cache policy: %r\n", Status));
    Cpu->SetMemoryAttributes = mSetAttributes;
    FreePool (mMemoryMap);
    mMemoryMap = NULL;
    mMemoryCount = 0;
  }
  return Status;
}
