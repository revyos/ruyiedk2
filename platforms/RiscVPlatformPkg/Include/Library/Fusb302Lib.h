// SPDX-License-Identifier: GPL-2.0-only
#ifndef FUSB302_LIB_H_
#define FUSB302_LIB_H_
#include <Uefi.h>

typedef EFI_STATUS (*FUSB302_TRANSFER) (
    VOID *Context, UINT8 Register, UINT8 *Value, BOOLEAN Read);

typedef enum {
  Fusb302Detached,
  Fusb302Cc1,
  Fusb302Cc2
} FUSB302_ORIENTATION;

typedef struct {
  FUSB302_TRANSFER Transfer;
  VOID *Context;
  FUSB302_ORIENTATION Candidate;
  FUSB302_ORIENTATION Orientation;
  UINT64 SinceNs;
  UINT8 Step;
  UINT8 Cc[2];
  UINT8 Vbus;
} FUSB302;

// A 5 V source with default USB current; no PD or VCONN negotiation.
EFI_STATUS Fusb302Init (FUSB302 *Port);
EFI_STATUS Fusb302Poll (FUSB302 *Port, FUSB302_ORIENTATION *Orientation);
#endif
