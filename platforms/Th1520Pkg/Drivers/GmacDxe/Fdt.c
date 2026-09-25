// SPDX-License-Identifier: BSD-2-Clause-Patent
// Discover controllers, shared MDIO wiring and RGMII pins from the DTB.
#include <PiDxe.h>
#include "Gmac.h"
#include <Guid/FdtHob.h>
#include <Library/FdtLib.h>
#include <Library/Th1520BoardSupportLib.h>
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
  while ((Current = FdtNodeOffsetByCompatible (Hw->Fdt, Current,
                                               "thead,th1520-gmac")) >= 0)
  {
    if (!EFI_ERROR (Registers (Hw->Fdt, Current, &Base, &Apb)) &&
        Base == Hw->Base)
    {
      *Node = Current;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
GmacParseHardware (FDT_CLIENT_PROTOCOL *Client, INT32 Node,
                   GMAC_HARDWARE *Hw)
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
  Hw->TxDelay = AsciiStrCmp (Mode, "rgmii-id") == 0 ||
                AsciiStrCmp (Mode, "rgmii-txid") == 0;
  Hw->RxDelay = AsciiStrCmp (Mode, "rgmii-id") == 0 ||
                AsciiStrCmp (Mode, "rgmii-rxid") == 0;
  if (!Hw->TxDelay && !Hw->RxDelay && AsciiStrCmp (Mode, "rgmii") != 0)
  {
    return EFI_UNSUPPORTED;
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
  Status = Th1520FdtPinctrl (Hw->Fdt, Node);
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
  return Th1520FdtPinctrl (Hw->Fdt, Node);
}
