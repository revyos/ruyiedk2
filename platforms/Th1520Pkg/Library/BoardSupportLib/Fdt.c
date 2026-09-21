// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "BoardFdt.h"

BOOLEAN
Th1520FdtEnabled (CONST VOID *Fdt, INT32 Node)
{
  CONST CHAR8 *Status;
  INT32 Length;
  if (Node < 0)
    return FALSE;
  for (; Node >= 0; Node = FdtParentOffset (Fdt, Node))
  {
    Status = FdtGetProp (Fdt, Node, "status", &Length);
    if (Status && !((Length == 5 && !CompareMem (Status, "okay", 5)) ||
                    (Length == 3 && !CompareMem (Status, "ok", 3))))
      return FALSE;
  }
  return TRUE;
}

EFI_STATUS
ReadCell (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT32 *Value)
{
  INT32 Size;
  CONST VOID *P = FdtGetProp (Fdt, Node, Name, &Size);
  if (!P)
    return EFI_NOT_FOUND;
  if (Size != 4)
    return EFI_COMPROMISED_DATA;
  *Value = Cell (P);
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520FdtReg (CONST VOID *Fdt, INT32 Node, UINTN *Base, UINTN *Size)
{
  CONST UINT32 *Reg;
  INT32 Parent, AddressCells, SizeCells, Bytes, Bus;
  UINT64 Address = 0, Length = 0;
  Parent = FdtParentOffset (Fdt, Node);
  if (Parent < 0)
    return EFI_UNSUPPORTED;
  AddressCells = FdtAddressCells (Fdt, Parent);
  SizeCells = FdtSizeCells (Fdt, Parent);
  if (AddressCells < 1 || AddressCells > 2 || SizeCells < 1 ||
      SizeCells > 2)
    return EFI_UNSUPPORTED;
  Reg = FdtGetProp (Fdt, Node, "reg", &Bytes);
  if (!Reg || Bytes < (AddressCells + SizeCells) * 4 ||
      Bytes % ((AddressCells + SizeCells) * 4))
    return EFI_COMPROMISED_DATA;
  for (INT32 I = 0; I < AddressCells; I++)
    Address = LShiftU64 (Address, 32) | Cell (Reg++);
  for (INT32 I = 0; I < SizeCells; I++)
    Length = LShiftU64 (Length, 32) | Cell (Reg++);
  // These controllers sit on the TH1520 identity-mapped SoC bus.
  for (Bus = Parent; Bus > 0; Bus = FdtParentOffset (Fdt, Bus))
    if (!FdtGetProp (Fdt, Bus, "ranges", &Bytes) || Bytes != 0)
      return EFI_UNSUPPORTED;
  if (!Length || Address > MAX_UINTN || Length > MAX_UINTN - Address)
    return EFI_UNSUPPORTED;
  *Base = (UINTN)Address;
  *Size = (UINTN)Length;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ClockProvider (CONST VOID *Fdt, INT32 Node, INT32 *Provider, UINT32 *Id)
{
  CONST UINT32 *Clocks;
  INT32 Length;
  UINT32 Cells;
  Clocks = FdtGetProp (Fdt, Node, "clocks", &Length);
  if (!Clocks || Length < 4)
    return EFI_NOT_FOUND;
  *Provider = FdtNodeOffsetByPhandle (Fdt, Cell (Clocks));
  if (!Th1520FdtEnabled (Fdt, *Provider) ||
      EFI_ERROR (ReadCell (Fdt, *Provider, "#clock-cells", &Cells)) ||
      Cells > 1 || Length != (INT32)((Cells + 1) * 4))
    return EFI_UNSUPPORTED;
  *Id = Cells ? Cell (Clocks + 1) : 0;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ApClock (CONST VOID *Fdt, INT32 Node, INT32 *Provider, UINT32 *Id,
         UINTN *Base)
{
  UINTN Size;
  EFI_STATUS Status = ClockProvider (Fdt, Node, Provider, Id);
  if (EFI_ERROR (Status))
    return Status;
  if (!Compatible (Fdt, *Provider, "thead,th1520-clk-ap"))
    return EFI_UNSUPPORTED;
  Status = Th1520FdtReg (Fdt, *Provider, Base, &Size);
  if (EFI_ERROR (Status) || *Base != 0xffef010000ULL || Size < 0x208)
    return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}

EFI_STATUS
EnableClock (CONST VOID *Fdt, INT32 Node, BOOLEAN Apply)
{
  // Clock IDs are the TH1520 AP binding ABI, not board assignments.
  STATIC CONST struct
  {
    UINT32 Id, Bit;
  } Gates[] = {{45, 24}, {47, 22}, {49, 20}, {51, 18}, {61, 8},
               {62, 7},  {63, 6},  {64, 5},  {65, 4},  {66, 3},
               {67, 2},  {68, 1},  {69, 0}};
  INT32 Provider;
  UINT32 Id;
  UINTN Base;
  EFI_STATUS Status = ClockProvider (Fdt, Node, &Provider, &Id);
  if (EFI_ERROR (Status))
    return Status;
  if (Compatible (Fdt, Provider, "fixed-clock"))
    return EFI_SUCCESS;
  Status = ApClock (Fdt, Node, &Provider, &Id, &Base);
  if (EFI_ERROR (Status))
    return Status;
  for (UINTN I = 0; I < ARRAY_SIZE (Gates); I++)
    if (Gates[I].Id == Id)
    {
      if (Apply)
        MmioOr32 (Base + 0x204, 1U << Gates[I].Bit);
      return EFI_SUCCESS;
    }
  return EFI_UNSUPPORTED;
}
