// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <assert.h>
#include <libfdt.h>
#include <stdio.h>
#include <string.h>

EFI_STATUS EFIAPI CpuInitialization (VOID *Fdt);
static UINT8 memory_bits, io_bits;

VOID EFIAPI BuildCpuHob (UINT8 memory, UINT8 io)
{
  memory_bits = memory;
  io_bits = io;
}
INTN EFIAPI HighBitSet64 (UINT64 value)
{
  return value ? 63 - __builtin_clzll (value) : -1;
}
UINT32 EFIAPI SwapBytes32 (UINT32 value)
{
  return __builtin_bswap32 (value);
}
UINT64 EFIAPI SwapBytes64 (UINT64 value)
{
  return __builtin_bswap64 (value);
}
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *data)
{
  UINT32 value;
  memcpy (&value, data, sizeof (value));
  return value;
}
UINT64 EFIAPI ReadUnaligned64 (CONST VOID *data)
{
  UINT64 value;
  memcpy (&value, data, sizeof (value));
  return value;
}
INTN EFIAPI CompareMem (CONST VOID *a, CONST VOID *b, UINTN size)
{
  return memcmp (a, b, size);
}
CONST VOID * EFIAPI
FdtGetProp (CONST VOID *fdt, INT32 node, CONST CHAR8 *name, INT32 *length)
{
  return fdt_getprop (fdt, node, name, length);
}
INT32 EFIAPI FdtParentOffset (CONST VOID *fdt, INT32 node)
{
  return fdt_parent_offset (fdt, node);
}
INT32 EFIAPI FdtNodeOffsetByPropValue (
    CONST VOID *fdt, INT32 start, CONST CHAR8 *name,
    CONST VOID *value, INT32 length)
{
  return fdt_node_offset_by_prop_value (fdt, start, name, value, length);
}

static int
tree (void *fdt, UINT64 base, UINT64 size)
{
  fdt32_t ranges[] = {
    cpu_to_fdt32 (0x01000000), 0, 0,
    cpu_to_fdt32 (base >> 32), cpu_to_fdt32 (base),
    cpu_to_fdt32 (size >> 32), cpu_to_fdt32 (size)
  };
  int node;
  assert (!fdt_create_empty_tree (fdt, 4096));
  assert (!fdt_setprop_u32 (fdt, 0, "#address-cells", 2));
  node = fdt_add_subnode (fdt, 0, "pcie");
  assert (node >= 0);
  assert (!fdt_setprop_string (fdt, node, "device_type", "pci"));
  assert (!fdt_setprop_u32 (fdt, node, "#address-cells", 3));
  assert (!fdt_setprop_u32 (fdt, node, "#size-cells", 2));
  assert (!fdt_setprop (fdt, node, "ranges", ranges, sizeof (ranges)));
  return node;
}

int main (void)
{
  unsigned char fdt[4096] __attribute__ ((aligned (8)));
  int node;
  tree (fdt, 0xffff0000ULL, 0x10000);
  assert (CpuInitialization (fdt) == EFI_SUCCESS && io_bits == 32);
  tree (fdt, 0x100000000ULL, 0x10000);
  assert (CpuInitialization (fdt) == EFI_SUCCESS && io_bits == 33);
  node = tree (fdt, 0x1800200000ULL, 0x10000);
  assert (CpuInitialization (fdt) == EFI_SUCCESS && io_bits == 37);
  assert (memory_bits == 40);
  assert (!fdt_setprop_string (fdt, node, "status", "disabled"));
  assert (CpuInitialization (fdt) == EFI_SUCCESS && io_bits == 32);
  tree (fdt, 1ULL << 40, 0x10000);
  assert (CpuInitialization (fdt) == EFI_UNSUPPORTED);
  tree (fdt, ~0ULL - 0x100, 0x10000);
  assert (CpuInitialization (fdt) == EFI_UNSUPPORTED);
  tree (fdt, 0, 0);
  assert (CpuInitialization (fdt) == EFI_UNSUPPORTED);
  assert (!fdt_create_empty_tree (fdt, sizeof (fdt)));
  assert (CpuInitialization (fdt) == EFI_SUCCESS && io_bits == 32);
  puts ("DT-derived CPU I/O space checks passed");
  return 0;
}
