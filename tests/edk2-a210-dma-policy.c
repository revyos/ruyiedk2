// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/A210Pkg/Drivers/CpuDmaDxe/CpuDma.c"
#include <assert.h>
#include <stdio.h>

static unsigned sequence, flushed, changed;
static EFI_STATUS flush_status;
static UINT64 flush_base, flush_size;

VOID EFIAPI MemoryFence (VOID)
{
}

static EFI_STATUS EFIAPI
flush (EFI_CPU_ARCH_PROTOCOL *cpu, EFI_PHYSICAL_ADDRESS base,
       UINT64 size, EFI_CPU_FLUSH_TYPE type)
{
  assert (type == EfiCpuFlushTypeWriteBackInvalidate);
  flushed = ++sequence;
  flush_base = base;
  flush_size = size;
  return flush_status;
}

static EFI_STATUS EFIAPI
set_attributes (EFI_CPU_ARCH_PROTOCOL *cpu, EFI_PHYSICAL_ADDRESS base,
                UINT64 size, UINT64 attributes)
{
  changed = ++sequence;
  return EFI_SUCCESS;
}

static void reset (void)
{
  sequence = flushed = changed = 0;
  flush_status = EFI_SUCCESS;
}

int main (void)
{
  EFI_CPU_ARCH_PROTOCOL cpu = {.FlushDataCache = flush};
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR map[] = {
    {.BaseAddress = 0x80000000, .Length = 0x10000000,
     .GcdMemoryType = EfiGcdMemoryTypeSystemMemory},
    {.BaseAddress = 0x40000000, .Length = 0x1000000,
     .GcdMemoryType = EfiGcdMemoryTypeMemoryMappedIo}
  };
  mMemoryMap = map;
  mMemoryCount = 2;
  mSetAttributes = set_attributes;
  reset ();
  assert (SetMemoryAttributes (&cpu, 0x80100000, 0x6000,
      EFI_MEMORY_WC | EFI_MEMORY_XP) == EFI_SUCCESS);
  assert (flushed && changed > flushed);
  assert (flush_base == 0x80100000 && flush_size == 0x6000);
  reset ();
  assert (SetMemoryAttributes (&cpu, 0x40000000, 0x1000,
      EFI_MEMORY_UC) == EFI_SUCCESS);
  assert (!flushed && changed);
  reset ();
  assert (SetMemoryAttributes (&cpu, 0x80100000, 0x6000,
      EFI_MEMORY_WB) == EFI_SUCCESS);
  assert (!flushed && changed);
  reset ();
  flush_status = EFI_DEVICE_ERROR;
  assert (SetMemoryAttributes (&cpu, 0x80100000, 0x6000,
      EFI_MEMORY_UC) == EFI_DEVICE_ERROR);
  assert (flushed && !changed);
  reset ();
  assert (SetMemoryAttributes (&cpu, ~0ULL, 2,
      EFI_MEMORY_UC) == EFI_INVALID_PARAMETER);
  assert (!flushed && !changed);
  puts ("A210 DMA cache-transition checks passed");
  return 0;
}
