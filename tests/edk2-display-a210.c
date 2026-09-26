// SPDX-License-Identifier: GPL-2.0-only
#include <Uefi.h>
#include <A210.h>
#include <Library/DisplayPlatformLib.h>
#include <assert.h>
#include <stdio.h>

static UINT64 parent = 1188000000;
static UINT32 clocks = 0x01ffffff;
static UINT32 divider = 0x10240902;
static UINT32 path = 0x22;

UINT64
A210DisplayClock (VOID)
{
  return parent;
}
UINT32 EFIAPI
MmioRead32 (UINTN address)
{
  switch (address - A210_VO_BASE)
  {
  case 0x200:
    return clocks;
  case 0x204:
    return divider;
  case 0x48:
    return path;
  default:
    assert (0);
    return 0;
  }
}
UINT32 EFIAPI
MmioWrite32 (UINTN address, UINT32 value)
{
  // All PLL registers and unrelated subsystem MMIO are forbidden.
  switch (address - A210_VO_BASE)
  {
  case 0x200:
    clocks = value;
    break;
  case 0x204:
    assert (!(clocks & BIT1));
    divider = value;
    break;
  case 0x48:
    path = value;
    break;
  default:
    assert (0);
  }
  return value;
}
UINT32 EFIAPI
MmioOr32 (UINTN address, UINT32 bits)
{
  return MmioWrite32 (address, MmioRead32 (address) | bits);
}
UINT32 EFIAPI
MmioAnd32 (UINTN address, UINT32 mask)
{
  return MmioWrite32 (address, MmioRead32 (address) & mask);
}
UINT32 EFIAPI
MmioAndThenOr32 (UINTN address, UINT32 mask, UINT32 bits)
{
  return MmioWrite32 (address, (MmioRead32 (address) & mask) | bits);
}
UINTN EFIAPI
MicroSecondDelay (UINTN delay)
{
  return delay;
}
int
main (void)
{
  const UINT32 rates[] = { 148500000, 74250000, 40000000, 25175000 };
  const UINT32 divisors[] = { 8, 16, 30, 48 };
  DISPLAY_HW hw = { 0 };
  DISPLAY_TIMING timing = { 0 };
  const UINT32 mask = 0xffU << 10;
  for (UINTN i = 0; i < ARRAY_SIZE (rates); i++)
  {
    UINT32 saved = divider;
    timing.Clock = rates[i];
    assert (DisplayPlatformPixelClock (&hw, &timing) == EFI_SUCCESS);
    assert (((divider & mask) >> 10) == divisors[i]);
    assert ((divider & ~(mask | BIT18)) == (saved & ~(mask | BIT18)));
    assert (divider & BIT18);
    assert (clocks == 0x01ffffff);
    assert (path == 0x26);
  }
  parent = 0;
  assert (DisplayPlatformPixelClock (&hw, &timing) == EFI_UNSUPPORTED);
  parent = 1188000000;
  timing.Clock = 0;
  assert (DisplayPlatformPixelClock (&hw, &timing) == EFI_UNSUPPORTED);
  timing.Clock = 1000000;
  assert (DisplayPlatformPixelClock (&hw, &timing) == EFI_UNSUPPORTED);
  puts ("A210 display: divider gating, mux preservation and PLL policy "
        "passed");
  return 0;
}
