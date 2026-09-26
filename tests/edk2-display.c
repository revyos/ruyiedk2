// SPDX-License-Identifier: GPL-2.0-only
#include "../platforms/RiscVPlatformPkg/Drivers/Dc8200Dxe/Display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size)
{
  return memcmp (A, B, Size);
}

VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset (P, 0, N); }

static void checksum (UINT8 *Block)
{
  unsigned sum = 0;
  for (int i = 0; i < 127; i++)
    sum += Block[i];
  Block[127] = -sum;
}

static DISPLAY_HW fixture (void)
{
  DISPLAY_HW hw = {0};
  const UINT8 header[] = {0, 255, 255, 255, 255, 255, 255, 0};
  const UINT8 cta[] = {2, 3, 11, 0, 0x42, 16, 4, 0x63, 3, 12, 0};
  memcpy (hw.Edid, header, sizeof (header));
  hw.Edid[18] = 1;
  hw.Edid[19] = 3;
  hw.Edid[126] = 1;
  memcpy (hw.Edid + 128, cta, sizeof (cta));
  hw.EdidSize = 256;
  checksum (hw.Edid);
  checksum (hw.Edid + 128);
  return hw;
}

static void dtd (UINT8 *d, const DISPLAY_TIMING *t)
{
  unsigned hb = t->HFront + t->HSync + t->HBack;
  unsigned vb = t->VFront + t->VSync + t->VBack;
  memset (d, 0, 18);
  d[0] = t->Clock / 10000;
  d[1] = (t->Clock / 10000) >> 8;
  d[2] = t->Width;
  d[3] = hb;
  d[4] = ((t->Width >> 8) << 4) | (hb >> 8);
  d[5] = t->Height;
  d[6] = vb;
  d[7] = ((t->Height >> 8) << 4) | (vb >> 8);
  d[8] = t->HFront;
  d[9] = t->HSync;
  d[10] = ((t->VFront & 15) << 4) | (t->VSync & 15);
  d[11] = ((t->HFront >> 8) << 6) | ((t->HSync >> 8) << 4) |
          ((t->VFront >> 4) << 2) | (t->VSync >> 4);
  d[17] = 0x18 | (t->HPositiveSync ? 2 : 0) |
          (t->VPositiveSync ? 4 : 0);
}

static void preferred_modes (void)
{
  DISPLAY_HW hw = fixture ();
  // Prefer 720p even when the same sink advertises 1080p.
  dtd (hw.Edid + 54, &DisplayTimings[1]);
  hw.Edid[24] = BIT1;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.ModeCount == 2);
  assert (hw.Modes[0].Width == 1280 && hw.Modes[1].Width == 1920);
  // EDID 1.4 makes the first DTD preferred even without the legacy bit.
  hw.Edid[24] = 0;
  hw.Edid[18] = 1;
  hw.Edid[19] = 4;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.Modes[0].Width == 1280);
  // A detailed mode absent from the static table, with mixed polarities.
  DISPLAY_TIMING custom = {
      1366, 768, 85500000, 70, 143, 213, 3, 3, 24, 0, TRUE, FALSE};
  dtd (hw.Edid + 54, &custom);
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.ModeCount == 3);
  const DISPLAY_TIMING *t = &hw.Modes[0];
  assert (t->Width == 1366 && t->Height == 768 && t->Clock == 85500000);
  assert (t->HFront == 70 && t->HSync == 143 && t->HBack == 213);
  assert (t->VFront == 3 && t->VSync == 3 && t->VBack == 24);
  assert (t->HPositiveSync && !t->VPositiveSync && !t->Vic);
  DISPLAY_HW copy = hw;
  // Borders and clocks outside the PHY table are unsupported.
  hw.Edid[69] = 1;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.Preferred && hw.ModeCount == 2);
  hw = copy;
  hw.Edid[54] = 0x20;
  hw.Edid[55] = 0x4e; // 200 MHz, still within framebuffer dimensions.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.Preferred && hw.ModeCount == 2);
  hw = copy;
  // Invalid blanking must not underflow into a huge back porch.
  hw.Edid[57] = 1;
  hw.Edid[58] &= 0xf0;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.Preferred && hw.ModeCount == 2);
  assert (hw.Modes[0].Width == 1920);
  custom.Width = 3840;
  custom.Height = 2160;
  custom.Clock = 594000000;
  dtd (hw.Edid + 54, &custom);
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.Preferred && hw.ModeCount == 2);
  // With no supported base preference, honor CTA's native 720p marker.
  hw.Edid[134] |= BIT7;
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.Modes[0].Width == 1280);
  // A valid base preference takes priority over a conflicting CTA marker.
  dtd (hw.Edid + 54, &DisplayTimings[0]);
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.Modes[0].Width == 1920);
  // Native CTA DTD without a base DTD or native SVD.
  hw = fixture ();
  hw.Edid[131] = 1;
  dtd (hw.Edid + 139, &DisplayTimings[1]);
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.Preferred && hw.ModeCount == 2);
  assert (hw.Modes[0].Width == 1280);
}

int main (void)
{
  preferred_modes ();
  DISPLAY_HW hw = fixture ();
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 2 && hw.HdmiSink);
  assert (hw.Modes[0].Width == 1920 && hw.Modes[1].Width == 1280);
  for (unsigned i = 0; i < 2; i++)
  {
    const DISPLAY_TIMING *t = &hw.Modes[i];
    assert ((t->Width + t->HFront + t->HSync + t->HBack) *
                (t->Height + t->VFront + t->VSync + t->VBack) * 60 ==
            t->Clock);
  }
  hw.Edid[133] |= 0x80; // CTA native VIC marker.
  hw.Edid[134] = 97;    // Unsupported 4K mode must not enter GOP.
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0].Width == 1920);
  hw.Edid[132] = 0x5f; // Truncated video data block.
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount && !hw.HdmiSink);
  hw = fixture ();
  hw.Edid[140] ^= 1; // Extension checksum failure.
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount);
  hw = fixture ();
  hw.Edid[0] = 1;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount && !hw.EdidSize);
  hw = fixture ();
  hw.Edid[0] ^= 1;
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount && !hw.EdidSize);
  hw = fixture ();
  hw.EdidSize = 127;
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount);
  // A base-block-only DVI display with an exact 1080p60 DTD.
  hw = fixture ();
  const UINT8 dtd[] = {0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58,
                       0x2c, 0x45, 0,    0,    0,    0,    0,    0,    0x1e};
  memcpy (hw.Edid + 54, dtd, sizeof (dtd));
  hw.Edid[126] = 0;
  hw.EdidSize = 128;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && !hw.HdmiSink);
  hw.Edid[71] |= 0x80; // Interlaced must not become progressive.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount);
  hw = fixture ();
  hw.Edid[35] = 0x21;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 4 && hw.Modes[2].Width == 800 &&
          hw.Modes[3].Width == 640);
  hw = fixture ();
  hw.Edid[38] = 800 / 8 - 31;
  hw.Edid[39] = 0x40;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 3 && hw.Modes[2].Width == 800);
  hw.Edid[39] = 0x4f; // 75 Hz must not become the 60 Hz mode.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 2);
  hw = fixture ();
  hw.Edid[133] = 1; // CTA VGA.
  hw.Edid[134] = 0; // Reserved VIC must not match SVGA's non-CTA marker.
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0].Width == 640);
  hw = fixture ();
  const UINT8 vga[] = {0xd6, 0x09, 0x80, 0xa0, 0x20, 0xe0, 0x2d, 0x10,
                      0x10, 0x60, 0xa2, 0, 0, 0, 0, 0, 0, 0x18};
  // Quantized 25.18 MHz, negative sync.
  memcpy (hw.Edid + 54, vga, sizeof (vga));
  hw.Edid[126] = 0;
  hw.EdidSize = 128;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0].Width == 640);
  hw.Edid[71] = 0x1e; // An explicitly advertised positive-sync DTD.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0].HPositiveSync);
  // Enumerate malformed CTA offsets and lengths under ASan/UBSan.
  for (unsigned offset = 0; offset < 256; offset++)
  {
    for (unsigned length = 0; length < 32; length++)
    {
      hw = fixture ();
      hw.Edid[130] = offset;
      hw.Edid[132] = 0x40 | length;
      checksum (hw.Edid + 128);
      DisplayParseEdid (&hw);
      assert (hw.ModeCount <= DISPLAY_MAX_MODES);
    }
  }
  puts ("HDMI EDID and pixel timing checks passed");
  return 0;
}
