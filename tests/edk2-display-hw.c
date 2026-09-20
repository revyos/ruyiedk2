// SPDX-License-Identifier: GPL-2.0-only
#include "../platforms/Th1520Pkg/Drivers/DisplayDxe/Display.h"
#include "../platforms/Th1520Pkg/Drivers/DisplayDxe/DwHdmiRegs.h"
#include <assert.h>
#include <stdio.h>

#define DC 0xffef600000ULL
#define HDMI 0xffef540000ULL
static struct
{
  UINTN Address;
  UINT32 Value;
} regs[256];
static unsigned count, phy_writes, pad_count;
static BOOLEAN connected = TRUE, pll_lock = TRUE, phy_lock = TRUE;
static UINT32 i2c_status = 2;

UINT32 EFIAPI MmioRead32 (UINTN address)
{
  if (address == DC + 0x20)
    return 0x8200;
  if (address == 0xffef010080ULL)
    return pll_lock ? BIT9 : 0;
  if (address == HDMI + 4 * HDMI_PHY_STAT0)
    return (connected ? 2 : 0) | (phy_lock ? 1 : 0);
  if (address == HDMI + 4 * HDMI_IH_I2CMPHY_STAT0)
    return i2c_status;
  for (unsigned i = 0; i < count; i++)
    if (regs[i].Address == address)
      return regs[i].Value;
  return 0;
}

UINT32 EFIAPI MmioWrite32 (UINTN address, UINT32 value)
{
  assert (address != 0xffef010030ULL && address != 0xffef010034ULL);
  assert (address != 0xffef010050ULL); // Never retune the DPU PLL.
  if (address == 0xffef010054ULL)
    assert (value == (MmioRead32 (address) & ~BIT29));
  if (address == HDMI + 4 * HDMI_PHY_I2CM_OPERATION_ADDR)
    phy_writes++;
  for (unsigned i = 0; i < count; i++)
  {
    if (regs[i].Address == address)
    {
      regs[i].Value = value;
      return value;
    }
  }
  assert (count < ARRAY_SIZE (regs));
  regs[count].Address = address;
  regs[count++].Value = value;
  return value;
}

UINT32 EFIAPI MmioOr32 (UINTN address, UINT32 bits)
{
  return MmioWrite32 (address, MmioRead32 (address) | bits);
}
UINT32 EFIAPI MmioAnd32 (UINTN address, UINT32 bits)
{
  return MmioWrite32 (address, MmioRead32 (address) & bits);
}
UINT32 EFIAPI MmioAndThenOr32 (UINTN address, UINT32 mask, UINT32 bits)
{
  return MmioWrite32 (address, (MmioRead32 (address) & mask) | bits);
}
UINTN EFIAPI MicroSecondDelay (UINTN delay)
{
  return delay;
}
VOID Th1520Pad (UINTN base, UINTN pin, UINT32 mux, UINT32 config)
{
  assert (base == 0xffec007000ULL);
  assert ((pin == 36 || pin == 37) && mux == 0 && config == (BIT9 | 3));
  pad_count++;
}

int main (void)
{
  DISPLAY_HW hw = {.Dc = DC, .Hdmi = HDMI, .HdmiSink = TRUE};
  regs[count++] = (typeof (regs[0])){0xffef010050ULL, 0x01206301};
  regs[count++] = (typeof (regs[0])){0xffef010054ULL, BIT24 | BIT25 | BIT29};
  assert (DisplayPrepare (&hw) == EFI_SUCCESS && pad_count == 2);
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_SUCCESS);
  assert (phy_writes == 12); // Six PHY parameters, required two passes.
  assert (MmioRead32 (DC + 0x1404) == 0xbf000000);
  assert (MmioRead32 (DC + 0x140c) == 1920 * 4);
  assert (MmioRead32 (DC + 0x1cd4) == 0xa);
  assert (MmioRead32 (DC + 0x1ccc) & BIT1);
  assert ((MmioRead32 (0xffef0101ecULL) & 0x1ff) == 0x108);
  assert (DisplaySetTiming (&hw, &DisplayTimings[1], 0xbf000000) == EFI_SUCCESS);
  assert ((MmioRead32 (0xffef0101ecULL) & 0x1ff) == 0x110);
  assert (DisplaySetTiming (&hw, &DisplayTimings[2], 0xbf000000) == EFI_SUCCESS);
  assert ((MmioRead32 (0xffef0101ecULL) & 0x1ff) == (BIT8 | 30));
  assert (MmioRead32 (0xffef010050ULL) == 0x01206301);
  assert (MmioRead32 (0xffef010054ULL) == (BIT24 | BIT25));
  assert (!(MmioRead32 (DC + 0x143c) & BIT31));
  assert (DisplaySetTiming (&hw, &DisplayTimings[3], 0xbf000000) == EFI_SUCCESS);
  assert ((MmioRead32 (0xffef0101ecULL) & 0x1ff) == (BIT8 | 48));
  assert (MmioRead32 (0xffef010054ULL) == (BIT24 | BIT25));
  assert (MmioRead32 (DC + 0x143c) & BIT31);
  assert (MmioRead32 (DC + 0x144c) & BIT31);
  assert (MmioRead32 (HDMI + 4 * HDMI_FC_INVIDCONF) == 0x18);
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_SUCCESS);
  assert (MmioRead32 (0xffef010054ULL) == (BIT24 | BIT25));
  assert (!(MmioRead32 (DC + 0x143c) & BIT31));
  // Equivalent power-on 1188 MHz PLL using fractional feedback is preserved.
  regs[0].Value = 0x01103101;
  regs[1].Value = 0x800000;
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_SUCCESS);
  assert (MmioRead32 (0xffef010050ULL) == 0x01103101);
  assert (MmioRead32 (0xffef010054ULL) == 0x800000);
  assert ((MmioRead32 (0xffef0101ecULL) & 0x1ff) == (BIT8 | 8));
  regs[0].Value = 0; // Invalid PLL configuration must not be guessed or rewritten.
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_UNSUPPORTED);
  regs[0].Value = 0x01206301;
  regs[1].Value = BIT24 | BIT25;
  i2c_status = 1; // NACK is not a successful PHY write.
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_DEVICE_ERROR);
  assert (!(MmioRead32 (DC + 0x1ccc) & BIT1));
  i2c_status = 0;
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_TIMEOUT);
  assert (!(MmioRead32 (DC + 0x1ccc) & BIT1));
  i2c_status = 2;
  phy_lock = FALSE;
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_TIMEOUT);
  pll_lock = FALSE;
  assert (DisplaySetTiming (&hw, &DisplayTimings[0], 0xbf000000) == EFI_TIMEOUT);
  connected = FALSE;
  assert (DisplayPrepare (&hw) == EFI_NOT_FOUND);
  puts ("HDMI register sequence and bounded failure checks passed");
  return 0;
}
