// SPDX-License-Identifier: GPL-2.0-only
#include <PiDxe.h>
#include <A210.h>
#include <Guid/FdtHob.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210ScmiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DisplayPlatformLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define HDMI_PHY_CONTROL (0x3000 * 4)
#define HDMI_PHY_RESET (0x4005 * 4)
#define HDMI_PHY_HPD_ENABLE BIT2
#define HDMI_PHY_SVSRET BIT5
#define PAD_MUX_OFFSET 0x400
#define PAD_HDMI_FUNCTION 4U
#define PAD_HDMI_CONFIG (BIT9 | 2U)

STATIC EFI_STATUS
FindRegisters (FDT_CLIENT_PROTOCOL *Fdt, CONST CHAR8 *Compatible,
               UINTN Expected, UINTN *Base, INT32 *Node)
{
  CONST VOID *Data;
  UINT32 Size;
  EFI_STATUS Status = Fdt->FindCompatibleNode (Fdt, Compatible, Node);
  if (EFI_ERROR (Status))
    return Status;
  Status = Fdt->GetNodeProperty (Fdt, *Node, "status", &Data, &Size);
  if (!EFI_ERROR (Status) && !(Size == 5 && !CompareMem (Data, "okay", 5))
      && !(Size == 3 && !CompareMem (Data, "ok", 3)))
    return EFI_NOT_FOUND;
  Status = Fdt->GetNodeProperty (Fdt, *Node, "reg", &Data, &Size);
  if (EFI_ERROR (Status) || Size < 16 || Size % 16
      || SwapBytes64 (ReadUnaligned64 (Data)) != Expected)
    return EFI_UNSUPPORTED;
  *Base = Expected;
  return EFI_SUCCESS;
}
EFI_STATUS
DisplayPlatformGetResources (FDT_CLIENT_PROTOCOL *Fdt, DISPLAY_HW *Hw)
{
  INT32 Node;
  VOID *Hob = GetFirstGuidHob (&gFdtHobGuid);
  EFI_STATUS Status;
  if (!Hob || GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))
    return EFI_NOT_FOUND;
  Hw->Fdt = (VOID *)(UINTN) * (UINT64 *)GET_GUID_HOB_DATA (Hob);
  Status = FindRegisters (Fdt, "zhihe,a210-dc8200", A210_DC_BASE, &Hw->Dc,
                          &Node);
  if (EFI_ERROR (Status))
    return Status;
  return FindRegisters (Fdt, "zhihe,a210-dw-hdmi", A210_HDMI_BASE,
                        &Hw->Hdmi, &Hw->HdmiNode);
}
STATIC EFI_STATUS
ConfigurePins (DISPLAY_HW *Hw)
{
  CONST UINT32 *Ref;
  CONST CHAR8 *Function, *Pins;
  INT32 Length, Group, Child, Pad;
  UINTN Base, Size;
  EFI_STATUS Status;
  UINT32 Seen = 0;
  Ref = FdtGetProp (Hw->Fdt, Hw->HdmiNode, "pinctrl-0", &Length);
  if (!Ref || Length != 4)
    return EFI_NOT_FOUND;
  Group = FdtNodeOffsetByPhandle (Hw->Fdt,
                                  SwapBytes32 (ReadUnaligned32 (Ref)));
  if (Group < 0)
    return EFI_NOT_FOUND;
  Pad = FdtParentOffset (Hw->Fdt, Group);
  if (EFI_ERROR (A210Reg (Hw->Fdt, Pad, 0, &Base, &Size))
      || Base != A210_PERI2_PAD_BASE)
    return EFI_UNSUPPORTED;
  Status = A210ScmiClocksEnable (Hw->Fdt, Pad);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Hw->Fdt, Pad);
  if (EFI_ERROR (Status))
    return Status;
  FdtForEachSubnode (Child, Hw->Fdt, Group)
  {
    Function = FdtGetProp (Hw->Fdt, Child, "function", &Length);
    if (!Function || Length != 5 || CompareMem (Function, "hdmi", 5))
      return EFI_UNSUPPORTED;
    Pins = FdtGetProp (Hw->Fdt, Child, "pins", &Length);
    for (INT32 Offset = 0; Pins && Offset < Length; Offset += 9)
    {
      UINTN Pin;
      if (Length - Offset < 9 || Pins[Offset + 8]
          || CompareMem (Pins + Offset, "GPIO2_", 6))
        return EFI_UNSUPPORTED;
      Pin = (Pins[Offset + 6] - '0') * 10 + Pins[Offset + 7] - '0';
      if (Pin != 25 && Pin != 30 && Pin != 31)
        return EFI_UNSUPPORTED;
      MmioAndThenOr32 (A210_PERI2_PAD_BASE + (Pin / 2) * 4,
                       ~(0x3ffU << ((Pin % 2) * 16)),
                       PAD_HDMI_CONFIG << ((Pin % 2) * 16));
      MmioAndThenOr32 (A210_PERI2_PAD_BASE + PAD_MUX_OFFSET
                           + (Pin / 8) * 4,
                       ~(0xfU << ((Pin % 8) * 4)),
                       PAD_HDMI_FUNCTION << ((Pin % 8) * 4));
      Seen |= 1U << Pin;
    }
  }
  return Seen == (BIT25 | BIT30 | BIT31) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
EFI_STATUS
DisplayPlatformPrepare (DISPLAY_HW *Hw)
{
  INT32 Dc = FdtNodeOffsetByCompatible (Hw->Fdt, -1, "zhihe,a210-dc8200");
  EFI_STATUS Status;
  if (!A210NodeEnabled (Hw->Fdt, Dc))
    return EFI_NOT_FOUND;
  Status = A210ScmiPowerOn (Hw->Fdt, Dc);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Hw->Fdt, Dc);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Hw->Fdt, Dc);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Hw->Fdt, Hw->HdmiNode);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Hw->Fdt, Hw->HdmiNode);
  if (EFI_ERROR (Status))
    return Status;
  MmioOr32 (Hw->Hdmi + HDMI_PHY_CONTROL,
            HDMI_PHY_HPD_ENABLE | HDMI_PHY_SVSRET);
  MmioWrite32 (Hw->Hdmi + HDMI_PHY_RESET, 0);
  MicroSecondDelay (10);
  return ConfigurePins (Hw);
}
EFI_STATUS
DisplayPlatformPixelClock (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing)
{
  if (!Timing->Clock)
    return EFI_INVALID_PARAMETER;
  return A210ScmiClockSet (Hw->Fdt, Hw->HdmiNode, "pix", Timing->Clock);
}
VOID
DisplayPlatformPhyConfig (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing,
                          UINT16 Parameters[6])
{
  (VOID) Hw;
  // Vendor Gen2 PHY table: 1080p uses a lower swing and TX_TRBON.
  if (Timing->Clock >= 148000000 && Timing->Clock <= 149000000)
  {
    Parameters[3] = 0x0180;
    Parameters[4] = 0x8188;
  }
}
