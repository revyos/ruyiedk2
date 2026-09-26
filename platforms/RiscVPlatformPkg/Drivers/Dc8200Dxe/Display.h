// SPDX-License-Identifier: GPL-2.0-only
#ifndef DC8200_DISPLAY_H
#define DC8200_DISPLAY_H
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseMemoryLib.h>

#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((MAX_UINT32 >> (31 - (h))) & (MAX_UINT32 << (l)))
#include <Library/DisplayPlatformLib.h>

extern CONST DISPLAY_TIMING DisplayTimings[DISPLAY_STANDARD_MODES];
BOOLEAN DisplayEdidValid (CONST UINT8 *Data);
VOID DisplayParseEdid (DISPLAY_HW *Hw);
BOOLEAN DisplayConnected (DISPLAY_HW *Hw);
EFI_STATUS DisplayPrepare (DISPLAY_HW *Hw);
EFI_STATUS DisplayReadEdid (DISPLAY_HW *Hw);
EFI_STATUS DisplaySetTiming (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing,
                             EFI_PHYSICAL_ADDRESS FrameBuffer);
VOID DisplayDisable (DISPLAY_HW *Hw);
#endif
