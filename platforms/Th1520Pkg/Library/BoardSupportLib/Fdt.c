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

INT32
FindEnabled (CONST VOID *Fdt, INT32 After, CONST CHAR8 *Name)
{
  while ((After = FdtNodeOffsetByCompatible (Fdt, After, Name)) >= 0)
    if (Th1520FdtEnabled (Fdt, After))
      break;
  return After;
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

EFI_STATUS
PwmClockRate (CONST VOID *Fdt, INT32 Node, UINT32 *Rate)
{
  INT32 Provider, Osc;
  CONST UINT32 *Clock;
  INT32 Bytes;
  UINT32 Id, Ref, Pll, Cfg, Ahb, Apb, Div;
  UINT64 Value;
  UINTN Base;
  EFI_STATUS Status = ApClock (Fdt, Node, &Provider, &Id, &Base);
  if (EFI_ERROR (Status) || Id != 51)
    return EFI_UNSUPPORTED;
  Clock = FdtGetProp (Fdt, Provider, "clocks", &Bytes);
  if (!Clock || Bytes != 4)
    return EFI_UNSUPPORTED;
  Osc = FdtNodeOffsetByPhandle (Fdt, Cell (Clock));
  if (!Th1520FdtEnabled (Fdt, Osc) ||
      !Compatible (Fdt, Osc, "fixed-clock") ||
      EFI_ERROR (ReadCell (Fdt, Osc, "clock-frequency", &Ref)) || !Ref)
    return EFI_UNSUPPORTED;
  Ahb = MmioRead32 (Base + 0x140);
  Apb = (MmioRead32 (Base + 0x150) & 7) + 1;
  Div = Ahb & 15;
  if (!Div)
    return EFI_DEVICE_ERROR;
  Value = Ref;
  if (!(Ahb & BIT5))
  {
    Pll = MmioRead32 (Base + 0x20);
    Cfg = MmioRead32 (Base + 0x24);
    if (!(Cfg & BIT30))
    {
      UINT32 PllDiv = (Pll & 63) * ((Pll >> 20) & 7) * ((Pll >> 24) & 7);
      Value = MultU64x32 (Ref, (Pll >> 8) & 4095);
      if (!PllDiv)
        return EFI_DEVICE_ERROR;
      if (!(Cfg & BIT24))
        Value += RShiftU64 (MultU64x32 (Ref, Cfg & 0xffffff), 24);
      Value = DivU64x32 (Value, PllDiv);
    }
  }
  Value = DivU64x32 (Value, Div * Apb);
  if (!Value || Value > MAX_UINT32)
    return EFI_DEVICE_ERROR;
  *Rate = (UINT32)Value;
  return EFI_SUCCESS;
}
