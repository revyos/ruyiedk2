// SPDX-License-Identifier: GPL-2.0-only
// CC measurement follows Linux drivers/usb/typec/tcpm/fusb302.c,
// Copyright 2016-2017 Google, Inc.
#include <Uefi.h>
#include <Library/Fusb302Lib.h>
#include <Library/TimerLib.h>

#define FUSB_DEVICE_ID 0x01
#define FUSB_SWITCHES0 0x02
#define FUSB_PULLUP_CC1 BIT6
#define FUSB_PULLUP_CC2 BIT7
#define FUSB_MEASURE_CC1 BIT2
#define FUSB_MEASURE_CC2 BIT3
#define FUSB_MEASURE 0x04
#define FUSB_CONTROL0 0x06
#define FUSB_MASK_INTERRUPTS BIT5
#define FUSB_USB_DEFAULT_CURRENT BIT2
#define FUSB_CONTROL2 0x08
#define FUSB_POWER 0x0b
#define FUSB_POWER_ALL 0x0f
#define FUSB_RESET 0x0c
#define FUSB_SOFTWARE_RESET BIT0
#define FUSB_STATUS0 0x40
#define FUSB_COMPARATOR BIT5
#define FUSB_VBUS_OK BIT7
// Comparator thresholds for the default-current Rp setting.
#define FUSB_RD_THRESHOLD 38 // 1.638 V: distinguish Open from Rd.
#define FUSB_RA_THRESHOLD 4  // 0.210 V: distinguish Rd from Ra.
#define CC_DEBOUNCE_NS 120000000ULL

typedef enum { CcOpen, CcRa, CcRd } CC_STATE;

STATIC EFI_STATUS
Write (FUSB302 *Port, UINT8 Register, UINT8 Value)
{
  return Port->Transfer (Port->Context, Register, &Value, FALSE);
}

EFI_STATUS
Fusb302Init (FUSB302 *Port)
{
  UINT8 Id;
  EFI_STATUS Status;
  STATIC CONST UINT8 Setup[][2] = {
    { FUSB_CONTROL2, 0 },
    { FUSB_CONTROL0, FUSB_MASK_INTERRUPTS | FUSB_USB_DEFAULT_CURRENT },
    { FUSB_POWER, FUSB_POWER_ALL },
    { FUSB_SWITCHES0, FUSB_PULLUP_CC1 | FUSB_PULLUP_CC2 }
  };
  UINTN Index;

  if (!Port || !Port->Transfer)
    return EFI_INVALID_PARAMETER;
  Port->Candidate = Port->Orientation = Fusb302Detached;
  Port->SinceNs = 0;
  Port->Step = 0;
  Status = Port->Transfer (Port->Context, FUSB_DEVICE_ID, &Id, TRUE);
  if (EFI_ERROR (Status))
    return Status;
  if (Id == 0 || Id == 0xff)
    return EFI_NOT_FOUND;
  Status = Write (Port, FUSB_RESET, FUSB_SOFTWARE_RESET);
  if (EFI_ERROR (Status))
    return Status;
  MicroSecondDelay (1000);
  for (Index = 0; Index < ARRAY_SIZE (Setup); Index++)
  {
    Status = Write (Port, Setup[Index][0], Setup[Index][1]);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Fusb302Poll (FUSB302 *Port, FUSB302_ORIENTATION *Orientation)
{
  FUSB302_ORIENTATION Candidate = Fusb302Detached;
  EFI_STATUS Status = EFI_SUCCESS;
  UINT8 Value = 0, Pin, Step;
  UINT64 Now;

  if (!Port || !Port->Transfer || !Orientation)
    return EFI_INVALID_PARAMETER;
  *Orientation = Port->Orientation;
  Pin = Port->Step / 5;
  Step = Port->Step % 5;
  if (Pin > 1)
    return EFI_INVALID_PARAMETER;
  // One I2C transfer per invocation. Comparator settling and CC debounce
  // happen between calls; no wait loop runs in a timer notification.
  switch (Step)
  {
    case 0:
      Status = Write (Port, FUSB_SWITCHES0,
                      FUSB_PULLUP_CC1 | FUSB_PULLUP_CC2
                      | (Pin ? FUSB_MEASURE_CC2 : FUSB_MEASURE_CC1));
      break;
    case 1:
      Status = Write (Port, FUSB_MEASURE, FUSB_RD_THRESHOLD);
      break;
    case 2:
      Status = Port->Transfer (Port->Context, FUSB_STATUS0, &Value, TRUE);
      Port->Vbus = Value & FUSB_VBUS_OK;
      if (Value & FUSB_COMPARATOR)
      {
        Port->Cc[Pin] = CcOpen;
        Port->Step += 2;
      }
      break;
    case 3:
      Status = Write (Port, FUSB_MEASURE, FUSB_RA_THRESHOLD);
      break;
    case 4:
      Status = Port->Transfer (Port->Context, FUSB_STATUS0, &Value, TRUE);
      Port->Vbus = Value & FUSB_VBUS_OK;
      Port->Cc[Pin] = Value & FUSB_COMPARATOR ? CcRd : CcRa;
      break;
  }
  if (EFI_ERROR (Status))
  {
    Port->Step = 0;
    Port->Candidate = Port->Orientation = Fusb302Detached;
    *Orientation = Fusb302Detached;
    return Status;
  }
  if (++Port->Step < 10)
    return EFI_SUCCESS;
  Port->Step = 0;
  Now = GetTimeInNanoSecond (GetPerformanceCounter ());
  if (Port->Cc[0] == CcRd && Port->Cc[1] != CcRd)
    Candidate = Fusb302Cc1;
  else if (Port->Cc[1] == CcRd && Port->Cc[0] != CcRd)
    Candidate = Fusb302Cc2;
  if (Candidate != Port->Candidate)
  {
    Port->Candidate = Candidate;
    Port->SinceNs = Now;
  }
  if (Candidate != Port->Orientation)
    Port->Orientation = Fusb302Detached;
  if (Candidate != Fusb302Detached
      && Now - Port->SinceNs >= CC_DEBOUNCE_NS
      && (Port->Orientation != Fusb302Detached || !Port->Vbus))
    Port->Orientation = Candidate;
  *Orientation = Port->Orientation;
  return EFI_SUCCESS;
}
