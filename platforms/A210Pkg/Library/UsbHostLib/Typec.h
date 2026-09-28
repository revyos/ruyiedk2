// SPDX-License-Identifier: GPL-2.0-only
#ifndef A210_USB_TYPEC_H_
#define A210_USB_TYPEC_H_
#include <Library/Fusb302Lib.h>

EFI_STATUS A210TypecInit (CONST VOID *Fdt, INT32 Controller, INT32 Phy);
EFI_STATUS A210TypecPoll (VOID);
VOID A210TypecStop (VOID);
VOID A210UsbOrientation (FUSB302_ORIENTATION Orientation);
#endif
