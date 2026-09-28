// SPDX-License-Identifier: GPL-2.0-only
#include <Library/Fusb302Lib.h>
#include <Library/TimerLib.h>
#include <assert.h>
#include <stdio.h>

static UINT64 now;
static UINT8 regs[256];
static unsigned cc[2]; /* 0: Open, 1: Ra, 2: Rd */
static BOOLEAN external_vbus, fail_io;
static unsigned transfers;

UINTN EFIAPI MicroSecondDelay (UINTN delay) { return delay; }
UINT64 EFIAPI GetPerformanceCounter (VOID) { return now; }
UINT64 EFIAPI GetTimeInNanoSecond (UINT64 ticks) { return ticks; }

static EFI_STATUS transfer (VOID *ctx, UINT8 reg, UINT8 *value,
                            BOOLEAN read)
{
  transfers++;
  if (fail_io)
    return EFI_DEVICE_ERROR;
  if (!read)
    regs[reg] = *value;
  else if (reg == 1)
    *value = 0x91;
  else if (reg == 0x40) {
    unsigned state = cc[(regs[2] & BIT3) != 0];
    *value = external_vbus ? BIT7 : 0;
    if (!state || (state == 2 && regs[4] == 4))
      *value |= BIT5;
  } else
    *value = regs[reg];
  return EFI_SUCCESS;
}

static FUSB302_ORIENTATION poll (FUSB302 *port, unsigned ms)
{
  FUSB302_ORIENTATION orientation;
  now += ms * 1000000ULL;
  do {
    unsigned before = transfers;
    assert (Fusb302Poll (port, &orientation) == EFI_SUCCESS);
    assert (transfers - before == 1);
  } while (port->Step);
  return orientation;
}

int main (void)
{
  FUSB302 port = {.Transfer = transfer};
  FUSB302_ORIENTATION orientation;
  assert (Fusb302Init (&port) == EFI_SUCCESS);
  assert (regs[6] == (BIT5 | BIT2));
  assert (poll (&port, 200) == Fusb302Detached);
  cc[0] = 2;
  assert (poll (&port, 0) == Fusb302Detached);
  assert (poll (&port, 119) == Fusb302Detached);
  assert (poll (&port, 1) == Fusb302Cc1);
  external_vbus = TRUE; /* Our already established source remains on. */
  assert (poll (&port, 10) == Fusb302Cc1);
  cc[0] = 0;
  assert (poll (&port, 10) == Fusb302Detached);
  external_vbus = FALSE;
  cc[1] = 2;
  assert (poll (&port, 10) == Fusb302Detached);
  assert (poll (&port, 120) == Fusb302Cc2);
  cc[0] = 2; /* Debug accessory: never treat two Rd as a USB device. */
  assert (poll (&port, 10) == Fusb302Detached);
  cc[0] = 1; cc[1] = 0;
  assert (poll (&port, 200) == Fusb302Detached);
  cc[0] = 0; cc[1] = 2; external_vbus = TRUE;
  assert (poll (&port, 10) == Fusb302Detached);
  assert (poll (&port, 200) == Fusb302Detached);
  external_vbus = FALSE;
  assert (poll (&port, 10) == Fusb302Cc2);
  fail_io = TRUE;
  assert (Fusb302Poll (&port, &orientation) == EFI_DEVICE_ERROR);
  assert (orientation == Fusb302Detached);
  fail_io = FALSE;
  assert (poll (&port, 10) == Fusb302Detached);
  assert (poll (&port, 120) == Fusb302Cc2);
  fail_io = TRUE;
  assert (Fusb302Init (&port) == EFI_DEVICE_ERROR);
  puts ("FUSB302: CC debounce, both orientations, detach and errors pass");
  return 0;
}
