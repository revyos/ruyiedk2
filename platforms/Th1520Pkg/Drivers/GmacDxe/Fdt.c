// SPDX-License-Identifier: BSD-2-Clause-Patent
// Discover controllers, shared MDIO wiring and RGMII pin groups from the DTB.
#include <PiDxe.h>
#include "Gmac.h"
#include <Guid/FdtHob.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>

STATIC UINT32
Cell (CONST VOID *Value)
{
  return SwapBytes32 (ReadUnaligned32 (Value));
}

STATIC EFI_STATUS
Registers (VOID *Fdt, INT32 Node, UINTN *Base, UINTN *Apb)
{
  INT32 Length;
  CONST UINT8 *Reg = FdtGetProp (Fdt, Node, "reg", &Length);
  if (Reg == NULL || Length != 32)
  {
    return EFI_UNSUPPORTED;
  }
  *Base = (UINTN)SwapBytes64 (ReadUnaligned64 ((CONST UINT64 *)Reg));
  *Apb = (UINTN)SwapBytes64 (ReadUnaligned64 ((CONST UINT64 *)(Reg + 16)));
  if ((*Base == 0xffe7070000ULL && *Apb == 0xffec003000ULL) ||
      (*Base == 0xffe7060000ULL && *Apb == 0xffec004000ULL))
  {
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}

EFI_STATUS
GmacFindNode (GMAC_HARDWARE *Hw, INT32 *Node)
{
  INT32 Current = -1;
  UINTN Base, Apb;
  while ((Current = FdtNodeOffsetByCompatible (Hw->Fdt, Current, "thead,th1520-gmac")) >= 0)
  {
    if (!EFI_ERROR (Registers (Hw->Fdt, Current, &Base, &Apb)) && Base == Hw->Base)
    {
      *Node = Current;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
GmacParseHardware (FDT_CLIENT_PROTOCOL *Client, INT32 Node, GMAC_HARDWARE *Hw)
{
  VOID *Hob;
  CONST UINT32 *Handle, *Address;
  CONST CHAR8 *Mode;
  INT32 Length, Phy, Mdio, Controller;
  UINTN Apb;
  EFI_STATUS Status;
  Hob = GetFirstGuidHob (&gFdtHobGuid);
  if (Hob == NULL || GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))
  {
    return EFI_NOT_FOUND;
  }
  Hw->Fdt = (VOID *)(UINTN) * (UINT64 *)GET_GUID_HOB_DATA (Hob);
  Status = Registers (Hw->Fdt, Node, &Hw->Base, &Hw->Apb);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Hw->Index = Hw->Base == 0xffe7070000ULL ? 0 : 1;
  Handle = FdtGetProp (Hw->Fdt, Node, "phy-handle", &Length);
  if (Handle == NULL || Length != 4)
  {
    return EFI_NOT_FOUND;
  }
  Phy = FdtNodeOffsetByPhandle (Hw->Fdt, Cell (Handle));
  if (Phy < 0)
  {
    return EFI_NOT_FOUND;
  }
  Address = FdtGetProp (Hw->Fdt, Phy, "reg", &Length);
  if (Address == NULL || Length != 4 || Cell (Address) > 31)
  {
    return EFI_UNSUPPORTED;
  }
  Hw->PhyAddress = Cell (Address);
  Mdio = FdtParentOffset (Hw->Fdt, Phy);
  Controller = Mdio < 0 ? Mdio : FdtParentOffset (Hw->Fdt, Mdio);
  if (Controller < 0)
  {
    return EFI_NOT_FOUND;
  }
  Status = Registers (Hw->Fdt, Controller, &Hw->MdioBase, &Apb);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Mode = FdtGetProp (Hw->Fdt, Node, "phy-mode", &Length);
  if (Mode == NULL || Length < 6 || Mode[Length - 1] != 0)
  {
    return EFI_UNSUPPORTED;
  }
  Hw->TxDelay = AsciiStrCmp (Mode, "rgmii-id") == 0 || AsciiStrCmp (Mode, "rgmii-txid") == 0;
  Hw->RxDelay = AsciiStrCmp (Mode, "rgmii-id") == 0 || AsciiStrCmp (Mode, "rgmii-rxid") == 0;
  if (!Hw->TxDelay && !Hw->RxDelay && AsciiStrCmp (Mode, "rgmii") != 0)
  {
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

STATIC INTN
PinNumber (CONST CHAR8 *Name)
{
  STATIC CONST CHAR8 *Mac0[] = {"GMAC0_TX_CLK", "GMAC0_RX_CLK", "GMAC0_TXEN", "GMAC0_TXD0",
                                "GMAC0_TXD1",   "GMAC0_TXD2",   "GMAC0_TXD3", "GMAC0_RXDV",
                                "GMAC0_RXD0",   "GMAC0_RXD1",   "GMAC0_RXD2", "GMAC0_RXD3",
                                "GMAC0_MDC",    "GMAC0_MDIO"};
  UINTN Index, Pin;
  CHAR8 *End;
  for (Index = 0; Index < ARRAY_SIZE (Mac0); Index++)
  {
    if (!AsciiStrCmp (Name, Mac0[Index]))
    {
      return 39 + Index;
    }
  }
  if (AsciiStrnCmp (Name, "GPIO2_", 6) && AsciiStrnCmp (Name, "GPIO3_", 6))
  {
    return -1;
  }
  if (Name[6] == 0 || EFI_ERROR (AsciiStrDecimalToUintnS (Name + 6, &End, &Pin)) || *End)
  {
    return -1;
  }
  if (Name[4] == '2' && ((Pin >= 18 && Pin <= 25) || Pin == 30 || Pin == 31))
  {
    return Pin;
  }
  if (Name[4] == '3' && Pin <= 1)
  {
    return 32 + Pin;
  }
  return -1;
}

STATIC EFI_STATUS
ConfigurePins (VOID *Fdt, INT32 Mac)
{
  CONST UINT32 *Groups, *Value;
  CONST CHAR8 *Names, *Function;
  CONST UINT8 *Reg;
  INT32 Length, GroupBytes, Pins, Child, Controller;
  UINTN Offset, NameOffset, NameSize, Drive, Shift, Mux;
  UINT64 Base;
  UINT32 Pad;
  INTN Pin;
  STATIC CONST UINT8 Strength[] = {1, 2, 3, 5, 7, 8, 10, 12, 13, 15, 16, 18, 20, 21, 23, 25};
  Groups = FdtGetProp (Fdt, Mac, "pinctrl-0", &GroupBytes);
  if (Groups == NULL || GroupBytes <= 0 || GroupBytes % 4)
  {
    return EFI_NOT_FOUND;
  }
  for (Offset = 0; Offset < (UINTN)GroupBytes / 4; Offset++)
  {
    Pins = FdtNodeOffsetByPhandle (Fdt, Cell (&Groups[Offset]));
    if (Pins < 0)
    {
      return EFI_NOT_FOUND;
    }
    Controller = FdtParentOffset (Fdt, Pins);
    Reg = FdtGetProp (Fdt, Controller, "reg", &Length);
    if (Reg == NULL || Length != 16)
    {
      return EFI_UNSUPPORTED;
    }
    Base = SwapBytes64 (ReadUnaligned64 ((CONST UINT64 *)Reg));
    if (Base != 0xffec007000ULL)
    {
      return EFI_UNSUPPORTED;
    }
    FdtForEachSubnode (Child, Fdt, Pins)
    {
      Function = FdtGetProp (Fdt, Child, "function", &Length);
      if (Function == NULL || Length < 1 || Function[Length - 1] != 0 ||
          (AsciiStrCmp (Function, "gmac0") && AsciiStrCmp (Function, "gmac1")))
      {
        return EFI_UNSUPPORTED;
      }
      Pad = 0;
      Value = FdtGetProp (Fdt, Child, "drive-strength", &Length);
      if (Value != NULL)
      {
        if (Length != 4 || Cell (Value) > 25)
        {
          return EFI_UNSUPPORTED;
        }
        for (Drive = 0; Drive < ARRAY_SIZE (Strength) - 1 && Cell (Value) > Strength[Drive];
             Drive++)
          ;
        Pad |= Drive;
      }
      if (FdtGetProp (Fdt, Child, "input-enable", NULL))
      {
        Pad |= BIT9;
      }
      if (FdtGetProp (Fdt, Child, "input-schmitt-enable", NULL))
      {
        Pad |= BIT7;
      }
      if (FdtGetProp (Fdt, Child, "bias-pull-up", NULL))
      {
        Pad |= BIT4 | BIT5;
      }
      if (FdtGetProp (Fdt, Child, "bias-pull-down", NULL))
      {
        Pad |= BIT4;
      }
      Value = FdtGetProp (Fdt, Child, "slew-rate", &Length);
      if (Value != NULL && Length == 4 && Cell (Value))
      {
        Pad |= BIT8;
      }
      Names = FdtGetProp (Fdt, Child, "pins", &Length);
      if (Names == NULL || Length <= 0)
      {
        return EFI_NOT_FOUND;
      }
      for (NameOffset = 0; NameOffset < (UINTN)Length; NameOffset += NameSize + 1)
      {
        NameSize = AsciiStrnLenS (Names + NameOffset, Length - NameOffset);
        if (NameSize == 0 || NameSize == Length - NameOffset)
        {
          return EFI_COMPROMISED_DATA;
        }
        Pin = PinNumber (Names + NameOffset);
        if (Pin < 0)
        {
          return EFI_UNSUPPORTED;
        }
        Mux = AsciiStrCmp (Function, "gmac0") == 0 ? 0 : (Pin >= 39 ? 2 : 1);
        if ((Mux == 0 && Pin < 39) || (Mux == 2 && Pin != 51 && Pin != 52))
        {
          return EFI_UNSUPPORTED;
        }
        Shift = 4 * (Pin % 8);
        MmioAndThenOr32 (Base + 0x400 + 4 * (Pin / 8), ~(15U << Shift), Mux << Shift);
        Shift = 16 * (Pin % 2);
        MmioAndThenOr32 (Base + 4 * (Pin / 2), ~(0x3ffU << Shift), Pad << Shift);
      }
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
GmacConfigurePins (GMAC_HARDWARE *Hw)
{
  GMAC_HARDWARE Mdio = *Hw;
  INT32 Node;
  EFI_STATUS Status;
  Status = GmacFindNode (Hw, &Node);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = ConfigurePins (Hw->Fdt, Node);
  if (EFI_ERROR (Status) || Hw->MdioBase == Hw->Base)
  {
    return Status;
  }
  Mdio.Base = Hw->MdioBase;
  Status = GmacFindNode (&Mdio, &Node);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  return ConfigurePins (Hw->Fdt, Node);
}
