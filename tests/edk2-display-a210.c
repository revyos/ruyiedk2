// SPDX-License-Identifier: GPL-2.0-only
#include <Uefi.h>
#include <Library/A210ScmiLib.h>
#include <Library/DisplayPlatformLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UINT64 requested;
static EFI_STATUS result;
static unsigned int calls;
static char tree;

EFI_STATUS
A210ScmiClockSet (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT64 Rate)
{
  assert (Fdt == &tree && Node == 42 && !strcmp (Name, "pix"));
  requested = Rate;
  calls++;
  return result;
}
UINT32 EFIAPI
MmioRead32 (UINTN Address)
{
  assert (!"pixel clock setup must not access native clock registers");
  return 0;
}
UINT32 EFIAPI
MmioWrite32 (UINTN Address, UINT32 Value)
{
  assert (!"pixel clock setup must not write native clock registers");
  return 0;
}
int
main (void)
{
  CONST UINT32 Rates[] = { 148500000, 74250000, 40000000, 25175000 };
  DISPLAY_HW Hw = { .Fdt = &tree, .HdmiNode = 42 };
  DISPLAY_TIMING Timing = { 0 };
  for (UINTN Index = 0; Index < ARRAY_SIZE (Rates); Index++)
  {
    Timing.Clock = Rates[Index];
    assert (DisplayPlatformPixelClock (&Hw, &Timing) == EFI_SUCCESS);
    assert (requested == Rates[Index] && calls == Index + 1);
  }
  result = EFI_DEVICE_ERROR;
  assert (DisplayPlatformPixelClock (&Hw, &Timing) == EFI_DEVICE_ERROR);
  Timing.Clock = 0;
  assert (DisplayPlatformPixelClock (&Hw, &Timing) == EFI_INVALID_PARAMETER);
  assert (calls == ARRAY_SIZE (Rates) + 1);
  puts ("A210 display: SCMI rate requests and error propagation passed");
  return 0;
}
