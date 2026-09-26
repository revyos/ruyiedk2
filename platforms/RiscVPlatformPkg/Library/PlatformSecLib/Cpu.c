/** @file
  Copyright (c) 2025, Ventana Micro Systems Inc. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformSecLib.h"
#include <Library/BaseLib.h>

STATIC UINT32
Cells (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Length;
  CONST UINT32 *Value = FdtGetProp (Fdt, Node, Name, &Length);
  return Value && Length == 4 ? SwapBytes32 (ReadUnaligned32 (Value))
                              : MAX_UINT32;
}

STATIC BOOLEAN
Enabled (CONST VOID *Fdt, INT32 Node)
{
  for (; Node >= 0; Node = FdtParentOffset (Fdt, Node))
  {
    INT32 Length;
    CONST CHAR8 *Status = FdtGetProp (Fdt, Node, "status", &Length);
    if (Status && !(Length == 5 && !CompareMem (Status, "okay", 5))
        && !(Length == 3 && !CompareMem (Status, "ok", 3)))
      return FALSE;
  }
  return TRUE;
}

/**
  Perform CPU initialization.

  @param  FdtPointer      The pointer to the device tree.

  @return EFI_SUCCESS     The platform initialized successfully.
  @retval  Others        - As the error code indicates

**/
EFI_STATUS
EFIAPI
CpuInitialization (
  VOID  *FdtPointer
  )
{
  UINT8 IoBits = 32;
  INT32 Node = -1;

  // PCI port I/O may be backed by MMIO above the 4 GiB boundary.
  while ((Node = FdtNodeOffsetByPropValue (
      FdtPointer, Node, "device_type", "pci", 4)) >= 0)
  {
    CONST UINT32 *Ranges;
    INT32 Length, Index;
    if (!Enabled (FdtPointer, Node))
      continue;
    if (Cells (FdtPointer, Node, "#address-cells") != 3
        || Cells (FdtPointer, Node, "#size-cells") != 2
        || Cells (FdtPointer, FdtParentOffset (FdtPointer, Node),
                   "#address-cells") != 2)
      continue;
    Ranges = FdtGetProp (FdtPointer, Node, "ranges", &Length);
    if (!Ranges || Length % 28)
      continue;
    for (Index = 0; Index < Length / 4; Index += 7)
    {
      UINT64 Base, Size, Limit;
      if ((SwapBytes32 (ReadUnaligned32 (Ranges + Index)) & 0x03000000)
          != 0x01000000)
        continue;
      Base = SwapBytes64 (ReadUnaligned64 (
          (CONST UINT64 *)(Ranges + Index + 3)));
      Size = SwapBytes64 (ReadUnaligned64 (
          (CONST UINT64 *)(Ranges + Index + 5)));
      if (!Size || Base > MAX_UINT64 - (Size - 1))
        return EFI_UNSUPPORTED;
      Limit = Base + Size - 1;
      IoBits = MAX (IoBits, HighBitSet64 (Limit) + 1);
    }
  }
  if (IoBits > 40)
    return EFI_UNSUPPORTED;
  //
  // for MMU type >= sv39
  //
  BuildCpuHob (40, IoBits);

  return EFI_SUCCESS;
}
