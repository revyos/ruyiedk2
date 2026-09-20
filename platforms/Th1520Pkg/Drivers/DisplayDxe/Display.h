// SPDX-License-Identifier: GPL-2.0-only
#ifndef TH1520_DISPLAY_H
#define TH1520_DISPLAY_H
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseMemoryLib.h>

#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((MAX_UINT32 >> (31 - (h))) & (MAX_UINT32 << (l)))
#define DISPLAY_MAX_MODES 4

typedef struct
{
  UINT32 Width, Height, Clock;
  UINT16 HFront, HSync, HBack, VFront, VSync, VBack;
  UINT8 Vic;
  BOOLEAN PositiveSync;
} DISPLAY_TIMING;

typedef struct
{
  UINTN Dc, Hdmi;
  UINT8 Edid[256];
  UINTN EdidSize;
  BOOLEAN HdmiSink;
  UINTN ModeCount;
  CONST DISPLAY_TIMING *Modes[DISPLAY_MAX_MODES];
} DISPLAY_HW;

extern CONST DISPLAY_TIMING DisplayTimings[DISPLAY_MAX_MODES];
BOOLEAN DisplayEdidValid (CONST UINT8 *Data);
VOID DisplayParseEdid (DISPLAY_HW *Hw);
EFI_STATUS DisplayPrepare (DISPLAY_HW *Hw);
EFI_STATUS DisplayReadEdid (DISPLAY_HW *Hw);
EFI_STATUS DisplaySetTiming (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing,
                             EFI_PHYSICAL_ADDRESS FrameBuffer);
VOID DisplayDisable (DISPLAY_HW *Hw);
#endif
