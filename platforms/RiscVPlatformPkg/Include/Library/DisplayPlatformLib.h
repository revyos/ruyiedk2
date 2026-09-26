// SPDX-License-Identifier: GPL-2.0-only
#ifndef DISPLAY_PLATFORM_LIB_H_
#define DISPLAY_PLATFORM_LIB_H_
#include <Uefi.h>
#include <Protocol/FdtClient.h>

#define DISPLAY_STANDARD_MODES 4
#define DISPLAY_MAX_MODES 16
#define DISPLAY_MAX_WIDTH 1920
#define DISPLAY_MAX_HEIGHT 1080

typedef struct
{
  UINT32 Width, Height, Clock;
  UINT16 HFront, HSync, HBack, VFront, VSync, VBack;
  UINT8 Vic;
  BOOLEAN HPositiveSync, VPositiveSync;
} DISPLAY_TIMING;

typedef struct
{
  UINTN Dc, Hdmi;
  VOID *Fdt;
  INT32 HdmiNode;
  UINT8 Edid[256];
  UINTN EdidSize;
  BOOLEAN HdmiSink, Preferred;
  UINTN ModeCount;
  DISPLAY_TIMING Modes[DISPLAY_MAX_MODES];
} DISPLAY_HW;

EFI_STATUS
DisplayPlatformGetResources (FDT_CLIENT_PROTOCOL *Fdt, DISPLAY_HW *Hw);
EFI_STATUS DisplayPlatformPrepare (DISPLAY_HW *Hw);
EFI_STATUS
DisplayPlatformPixelClock (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing);
VOID DisplayPlatformPhyConfig (DISPLAY_HW *Hw,
                               CONST DISPLAY_TIMING *Timing,
                               UINT16 Parameters[6]);
#endif
