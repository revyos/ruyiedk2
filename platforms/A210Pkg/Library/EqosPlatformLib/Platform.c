// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <A210.h>
#include <Guid/FdtHob.h>
#include <Library/A210ScmiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/EqosPlatformLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define GMAC_CLOCK_CONTROL 0x00
#define GMAC_INTERFACE 0x04
#define GMAC_CLOCK_ENABLE 0x1f
#define GMAC_SPEED_MASK (3U << 8)
#define GMAC_RGMII 1
#define PAD_MUX_OFFSET 0x400
#define PAD_INPUT BIT9
#define PAD_CONFIG_MASK 0x3ff

STATIC UINT32
Cell (CONST VOID *Data)
{
  return SwapBytes32 (ReadUnaligned32 (Data));
}
STATIC INT32
Reference (VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Length;
  CONST UINT32 *Value = FdtGetProp (Fdt, Node, Name, &Length);
  return Value && Length == 4 ? FdtNodeOffsetByPhandle (Fdt, Cell (Value))
                              : -1;
}
STATIC UINT64
RegBase (VOID *Fdt, INT32 Node)
{
  INT32 Length;
  CONST UINT64 *Value = FdtGetProp (Fdt, Node, "reg", &Length);
  return Value && Length == 16 ? SwapBytes64 (ReadUnaligned64 (Value))
                               : MAX_UINT64;
}
STATIC BOOLEAN
StringIs (VOID *Fdt, INT32 Node, CONST CHAR8 *Name, CONST CHAR8 *Expected)
{
  INT32 Length;
  CONST CHAR8 *Text = FdtGetProp (Fdt, Node, Name, &Length);
  return Text && Length == AsciiStrSize (Expected)
         && CompareMem (Text, Expected, Length) == 0;
}
EFI_STATUS
GmacParseHardware (FDT_CLIENT_PROTOCOL *Client, INT32 Node,
                   GMAC_HARDWARE *Hw)
{
  VOID *Hob = GetFirstGuidHob (&gFdtHobGuid);
  INT32 Phy, Sys, Length;
  CONST UINT32 *Address;
  CONST CHAR8 *Compatible;
  if (!Hob || GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))
    return EFI_NOT_FOUND;
  Hw->Fdt = (VOID *)(UINTN) * (UINT64 *)GET_GUID_HOB_DATA (Hob);
  if (!StringIs (Hw->Fdt, Node, "status", "okay")
      && !StringIs (Hw->Fdt, Node, "status", "ok")
      && FdtGetProp (Hw->Fdt, Node, "status", NULL))
    return EFI_NOT_FOUND;
  Compatible = FdtGetProp (Hw->Fdt, Node, "compatible", &Length);
  if (!Compatible
      || !FdtStringListContains (Compatible, Length, "zhihe,a210-dwmac"))
    return EFI_UNSUPPORTED;
  Hw->Base = RegBase (Hw->Fdt, Node);
  if (Hw->Base != A210_GMAC0_BASE && Hw->Base != A210_GMAC1_BASE)
    return EFI_UNSUPPORTED;
  Hw->Index = Hw->Base == A210_GMAC1_BASE;
  Sys = Reference (Hw->Fdt, Node, "zhihe,gmacsys");
  Phy = Reference (Hw->Fdt, Node, "phy-handle");
  if (Sys < 0 || Phy < 0)
    return EFI_NOT_FOUND;
  Hw->Apb = RegBase (Hw->Fdt, Sys);
  if (Hw->Apb != Hw->Base + 0xf000)
    return EFI_UNSUPPORTED;
  Address = FdtGetProp (Hw->Fdt, Phy, "reg", &Length);
  if (!Address || Length != 4 || Cell (Address) > 31)
    return EFI_UNSUPPORTED;
  Hw->PhyAddress = Cell (Address);
  if (FdtParentOffset (Hw->Fdt, FdtParentOffset (Hw->Fdt, Phy)) != Node)
    return EFI_UNSUPPORTED;
  Hw->MdioBase = Hw->Base;
  Hw->TxDelay = StringIs (Hw->Fdt, Node, "phy-mode", "rgmii-id")
                || StringIs (Hw->Fdt, Node, "phy-mode", "rgmii-txid");
  Hw->RxDelay = StringIs (Hw->Fdt, Node, "phy-mode", "rgmii-id")
                || StringIs (Hw->Fdt, Node, "phy-mode", "rgmii-rxid");
  if (!Hw->TxDelay && !Hw->RxDelay
      && !StringIs (Hw->Fdt, Node, "phy-mode", "rgmii"))
    return EFI_UNSUPPORTED;
  Hw->Node = Node;
  return EFI_SUCCESS;
}
STATIC EFI_STATUS
ConfigurePins (GMAC_HARDWARE *Hw)
{
  STATIC CONST UINT8 DriveMa[]
      = { 1, 2, 3, 5, 7, 8, 10, 12, 13, 15, 16, 18, 20, 21, 23, 25 };
  INT32 Group = Reference (Hw->Fdt, Hw->Node, "pinctrl-0");
  INT32 Child, Length;
  EFI_STATUS Status;
  UINT32 Seen = 0;
  if (Group < 0
      || RegBase (Hw->Fdt, FdtParentOffset (Hw->Fdt, Group))
             != A210_PERI1_PAD_BASE)
    return EFI_UNSUPPORTED;
  Status = A210ScmiClocksEnable (Hw->Fdt, FdtParentOffset (Hw->Fdt, Group));
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Hw->Fdt, FdtParentOffset (Hw->Fdt, Group));
  if (EFI_ERROR (Status))
    return Status;
  FdtForEachSubnode (Child, Hw->Fdt, Group)
  {
    CONST CHAR8 *Pins = FdtGetProp (Hw->Fdt, Child, "pins", &Length);
    CONST UINT32 *Drive;
    UINT32 Value = 0, Strength;
    INT32 Bytes, Offset = 0;
    if (!Pins
        || !StringIs (Hw->Fdt, Child, "function",
                      Hw->Index ? "gmac1" : "gmac0")
        || !FdtGetProp (Hw->Fdt, Child, "bias-disable", NULL))
      return EFI_UNSUPPORTED;
    Drive = FdtGetProp (Hw->Fdt, Child, "drive-strength", &Bytes);
    if (!Drive || Bytes != 4 || Cell (Drive) > 25)
      return EFI_UNSUPPORTED;
    Strength = Cell (Drive);
    while (DriveMa[Value] < Strength)
      Value++;
    if (FdtGetProp (Hw->Fdt, Child, "input-enable", NULL))
      Value |= PAD_INPUT;
    if (FdtGetProp (Hw->Fdt, Child, "input-schmitt-enable", NULL))
      return EFI_UNSUPPORTED;
    Drive = FdtGetProp (Hw->Fdt, Child, "slew-rate", &Bytes);
    if (!Drive || Bytes != 4 || Cell (Drive))
      return EFI_UNSUPPORTED;
    while (Offset < Length)
    {
      UINTN Pin = 0, End = Offset, Start;
      while (End < Length && Pins[End])
        End++;
      if (End == Length || End - Offset < 7
          || CompareMem (Pins + Offset, Hw->Index ? "GPIO1_" : "GPIO0_",
                         6))
        return EFI_UNSUPPORTED;
      for (Start = Offset + 6; Start < End; Start++)
      {
        if (Pins[Start] < '0' || Pins[Start] > '9')
          return EFI_UNSUPPORTED;
        Pin = Pin * 10 + Pins[Start] - '0';
        if (Pin > (Hw->Index ? 15 : 13))
          return EFI_UNSUPPORTED;
      }
      if (Hw->Index && Pin < 2)
        return EFI_UNSUPPORTED;
      Seen |= 1U << (Pin - (Hw->Index ? 2 : 0));
      Pin += Hw->Index * 32;
      MmioAndThenOr32 (A210_PERI1_PAD_BASE + (Pin / 2) * 4,
                       ~(PAD_CONFIG_MASK << ((Pin % 2) * 16)),
                       Value << ((Pin % 2) * 16));
      MmioAndThenOr32 (A210_PERI1_PAD_BASE + PAD_MUX_OFFSET
                           + (Pin / 8) * 4,
                       ~(0xfU << ((Pin % 8) * 4)), 1U << ((Pin % 8) * 4));
      Offset = End + 1;
    }
  }
  return Seen == 0x3fff ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
EFI_STATUS
GmacSetSpeed (GMAC_HARDWARE *Hw, UINTN Speed)
{
  UINT32 Value;
  switch (Speed)
  {
  case 1000:
    Value = 0;
    break;
  case 100:
    Value = 3U << 8;
    break;
  case 10:
    Value = 2U << 8;
    break;
  default:
    return EFI_UNSUPPORTED;
  }
  MmioAndThenOr32 (Hw->Apb + GMAC_CLOCK_CONTROL, ~GMAC_SPEED_MASK, Value);
  return EFI_SUCCESS;
}
EFI_STATUS
GmacPlatformInitialize (GMAC_HARDWARE *Hw)
{
  UINT64 Rate;
  EFI_STATUS Status = A210ScmiPowerOn (Hw->Fdt, Hw->Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Hw->Fdt, Hw->Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Hw->Fdt, Hw->Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClockGet (Hw->Fdt, Hw->Node, "stmmaceth", &Rate);
  if (EFI_ERROR (Status))
    return Status;
  if (!Rate || Rate > 500000000)
    return EFI_UNSUPPORTED;
  Hw->Clock = (UINT32)Rate;
  Status = ConfigurePins (Hw);
  if (EFI_ERROR (Status))
    return Status;
  MmioOr32 (Hw->Apb + GMAC_CLOCK_CONTROL, GMAC_CLOCK_ENABLE);
  MmioAndThenOr32 (Hw->Apb + GMAC_INTERFACE, ~0xfU, GMAC_RGMII);
  MicroSecondDelay (100);
  return GmacSetSpeed (Hw, 1000);
}
