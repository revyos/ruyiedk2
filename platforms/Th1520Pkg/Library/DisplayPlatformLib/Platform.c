// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
// TH1520 display clock and pad setup from the validated GOP driver.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DisplayPlatformLib.h>
#include <Library/IoLib.h>
#include <Library/Th1520BoardSupportLib.h>
#include <Library/TimerLib.h>

#define BIT(n) (1U << (n))
#define AP 0xffef010000ULL
#define VO 0xffef528000ULL

STATIC EFI_STATUS
FindDevice (FDT_CLIENT_PROTOCOL *Fdt, CONST CHAR8 *Compatible,
            UINTN Expected, UINTN *Base)
{
  INT32 Node;
  CONST VOID *Property;
  UINT32 Size;
  EFI_STATUS Status;
  Status = Fdt->FindCompatibleNode (Fdt, Compatible, &Node);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "status", &Property, &Size);
  if (!EFI_ERROR (Status)
      && !((Size == 5 && CompareMem (Property, "okay", 5) == 0)
           || (Size == 3 && CompareMem (Property, "ok", 3) == 0)))
  {
    return EFI_NOT_FOUND;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "reg", &Property, &Size);
  if (EFI_ERROR (Status) || Size != 16
      || SwapBytes64 (ReadUnaligned64 (Property)) != Expected)
  {
    return EFI_UNSUPPORTED;
  }
  *Base = Expected;
  return EFI_SUCCESS;
}

EFI_STATUS
DisplayPlatformGetResources (FDT_CLIENT_PROTOCOL *Fdt, DISPLAY_HW *Hw)
{
  EFI_STATUS Status
      = FindDevice (Fdt, "thead,th1520-dc8200", 0xffef600000ULL, &Hw->Dc);
  if (EFI_ERROR (Status))
    return Status;
  return FindDevice (Fdt, "thead,th1520-dw-hdmi", 0xffef540000ULL,
                     &Hw->Hdmi);
}
EFI_STATUS
DisplayPlatformPrepare (DISPLAY_HW *Hw)
{
  (VOID) Hw;
  // The M-mode platform releases VO and configures both display IOPMPs.
  // Keep GPU clocks, reset and power domain untouched.
  MmioOr32 (VO + 0x50, BIT (0) | BIT (6) | BIT (7) | BIT (8) | BIT (9)
                           | BIT (10) | BIT (11) | BIT (20) | BIT (21)
                           | BIT (22) | BIT (27) | BIT (28));
  MmioOr32 (VO + 0x54, BIT (0));
  MmioOr32 (VO + 0x04, 7);
  MmioOr32 (VO + 0x14, 3);
  MicroSecondDelay (10);
  // HDMI DDC pins (pad group 3, SCL and SDA); preserve CEC and fan.
  Th1520Pad (0xffec007000ULL, 36, 0, BIT9 | 3);
  Th1520Pad (0xffec007000ULL, 37, 0, BIT9 | 3);
  return EFI_SUCCESS;
}

EFI_STATUS
DisplayPlatformPixelClock (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *T)
{
  (VOID) Hw;
  UINTN Try;
  UINT32 Cfg0, Cfg1, Ref, Post1, Post2, Divider;
  UINT64 Feedback, Rate;
  // The PLL is shared with the subsequent OS driver. Preserve its power-on
  // reference/feedback/postdividers and fractional mode across GOP modes.
  Cfg0 = MmioRead32 (AP + 0x50);
  Cfg1 = MmioRead32 (AP + 0x54);
  Ref = Cfg0 & 0x3f;
  Post1 = (Cfg0 >> 20) & 7;
  Post2 = (Cfg0 >> 24) & 7;
  if (Ref == 0 || Post1 == 0 || Post2 == 0 || (Cfg1 & BIT (30)))
  {
    return EFI_UNSUPPORTED;
  }
  Feedback = (UINT64)((Cfg0 >> 8) & 0xfff) << 24;
  if (!(Cfg1 & BIT (24)))
  {
    Feedback += Cfg1 & 0xffffff;
  }
  Rate = 24000000ULL * Feedback / ((UINT64)Ref * Post1 * Post2 << 24);
  // CLK_DIVIDER_ONE_BASED without ROUND_CLOSEST: round the divider up.
  // With the 1188 MHz PLL this yields 148.5/74.25/39.6/24.75 MHz.
  if (Rate == 0 || T->Clock == 0 || (Rate + T->Clock - 1) / T->Clock > 255)
  {
    return EFI_UNSUPPORTED;
  }
  Divider = (UINT32)((Rate + T->Clock - 1) / T->Clock);
  // Enable a powered-down PLL without retuning it.
  MmioAnd32 (AP + 0x54, ~BIT (29));
  for (Try = 0; Try < 100; Try++)
  {
    if (MmioRead32 (AP + 0x80) & BIT (9))
    {
      MicroSecondDelay (30);
      MmioAnd32 (AP + 0x1ec, ~BIT (8));
      MicroSecondDelay (1);
      MmioAndThenOr32 (AP + 0x1ec, ~0xffU, Divider);
      MmioOr32 (AP + 0x1ec, BIT (8));
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}

VOID
DisplayPlatformPhyConfig (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing,
                          UINT16 Parameters[6])
{
  (VOID) Hw;
  (VOID) Timing;
  (VOID) Parameters;
}
