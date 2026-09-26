// SPDX-License-Identifier: GPL-2.0-only
#include <PiDxe.h>
#include <A210.h>
#include <Guid/FdtHob.h>
#include <Library/A210ClockLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DisplayPlatformLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define VO_CLOCK_ENABLE 0x200
#define VO_RESET_N 0x400
#define VO_PATH_CONTROL 0x48
#define VO_PIXEL_DIVIDER 0x204
#define VO_HDMI_SOURCE_MASK (3U << 2)
#define VO_HDMI_SOURCE_CH1 (1U << 2)
#define VO_CH1_DIV_MASK (0xffU << 10)
#define VO_CH1_DIV_ENABLE BIT18
#define VO_DISPLAY_CLOCKS                                                 \
  (BIT1 | BIT2 | BIT3 | BIT4 | BIT8 | BIT11 | BIT18 | BIT19 | BIT20       \
   | BIT21 | BIT23 | BIT24)
#define VO_DISPLAY_RESETS                                                 \
  (BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT8 | BIT9 | BIT15 | BIT16 | BIT17 \
   | BIT18)
#define VO_PCU_ACTIVE 0x10
#define HDMI_PHY_CONTROL (0x3000 * 4)
#define HDMI_PHY_RESET (0x4005 * 4)
#define HDMI_PHY_HPD_ENABLE BIT2
#define HDMI_PHY_SVSRET BIT5
#define PAD_CLOCK_ENABLE (A210_PERI2_BASE + 0x200)
#define PAD_RESET_N (A210_PERI2_BASE + 0x400)
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
  INT32 Length, Group, Child;
  UINT32 Seen = 0;
  Ref = FdtGetProp (Hw->Fdt, Hw->HdmiNode, "pinctrl-0", &Length);
  if (!Ref || Length != 4)
    return EFI_NOT_FOUND;
  Group = FdtNodeOffsetByPhandle (Hw->Fdt,
                                  SwapBytes32 (ReadUnaligned32 (Ref)));
  if (Group < 0)
    return EFI_NOT_FOUND;
  MmioOr32 (PAD_CLOCK_ENABLE, BIT13);
  MmioOr32 (PAD_RESET_N, BIT18);
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
  if (!(MmioRead32 (A210_VO_PCU_BASE + VO_PCU_ACTIVE) & BIT0))
    return EFI_NOT_READY;
  MmioOr32 (A210_VO_BASE + VO_CLOCK_ENABLE, VO_DISPLAY_CLOCKS);
  MmioOr32 (A210_VO_BASE + VO_RESET_N, VO_DISPLAY_RESETS);
  MmioOr32 (Hw->Hdmi + HDMI_PHY_CONTROL,
            HDMI_PHY_HPD_ENABLE | HDMI_PHY_SVSRET);
  MmioWrite32 (Hw->Hdmi + HDMI_PHY_RESET, 0);
  MicroSecondDelay (10);
  return ConfigurePins (Hw);
}
EFI_STATUS
DisplayPlatformPixelClock (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing)
{
  UINT64 Parent = A210DisplayClock ();
  UINT32 Divider;
  (VOID) Hw;
  if (!Parent || !Timing->Clock)
    return EFI_UNSUPPORTED;
  Divider = (UINT32)((Parent + Timing->Clock - 1) / Timing->Clock);
  if (Divider < 2 || Divider > 255)
    return EFI_UNSUPPORTED;
  // Latch only the channel divider; retain the power-on DPU PLL.
  MmioAnd32 (A210_VO_BASE + VO_CLOCK_ENABLE, ~BIT1);
  MmioAnd32 (A210_VO_BASE + VO_PIXEL_DIVIDER, ~VO_CH1_DIV_ENABLE);
  MmioAndThenOr32 (A210_VO_BASE + VO_PIXEL_DIVIDER, ~VO_CH1_DIV_MASK,
                   Divider << 10);
  MmioOr32 (A210_VO_BASE + VO_PIXEL_DIVIDER, VO_CH1_DIV_ENABLE);
  MmioAndThenOr32 (A210_VO_BASE + VO_PATH_CONTROL, ~VO_HDMI_SOURCE_MASK,
                   VO_HDMI_SOURCE_CH1);
  MmioOr32 (A210_VO_BASE + VO_CLOCK_ENABLE, BIT1);
  MicroSecondDelay (10);
  return EFI_SUCCESS;
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
