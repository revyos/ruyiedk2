// SPDX-License-Identifier: GPL-2.0-only
#include "../platforms/Th1520Pkg/Drivers/DisplayDxe/Display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size)
{
  return memcmp (A, B, Size);
}

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
  hw.Edid[126] = 1;
  memcpy (hw.Edid + 128, cta, sizeof (cta));
  hw.EdidSize = 256;
  checksum (hw.Edid);
  checksum (hw.Edid + 128);
  return hw;
}

int main (void)
{
  DISPLAY_HW hw = fixture ();
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 2 && hw.HdmiSink);
  assert (hw.Modes[0]->Width == 1920 && hw.Modes[1]->Width == 1280);
  for (unsigned i = 0; i < 2; i++)
  {
    const DISPLAY_TIMING *t = hw.Modes[i];
    assert ((t->Width + t->HFront + t->HSync + t->HBack) *
                (t->Height + t->VFront + t->VSync + t->VBack) * 60 ==
            t->Clock);
  }
  hw.Edid[133] |= 0x80; // CTA native VIC marker.
  hw.Edid[134] = 97;    // Unsupported 4K mode must not enter GOP.
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0]->Width == 1920);
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
  assert (hw.ModeCount == 4 && hw.Modes[2]->Width == 800 && hw.Modes[3]->Width == 640);
  hw = fixture ();
  hw.Edid[38] = 800 / 8 - 31;
  hw.Edid[39] = 0x40;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 3 && hw.Modes[2]->Width == 800);
  hw.Edid[39] = 0x4f; // 75 Hz must not become the 60 Hz mode.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 2);
  hw = fixture ();
  hw.Edid[133] = 1; // CTA VGA.
  hw.Edid[134] = 0; // Reserved VIC must not match SVGA's non-CTA marker.
  checksum (hw.Edid + 128);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0]->Width == 640);
  hw = fixture ();
  const UINT8 vga[] = {0xd6, 0x09, 0x80, 0xa0, 0x20, 0xe0, 0x2d, 0x10,
                      0x10, 0x60, 0xa2, 0, 0, 0, 0, 0, 0, 0x18};
  memcpy (hw.Edid + 54, vga, sizeof (vga)); // Quantized 25.18 MHz, negative sync.
  hw.Edid[126] = 0;
  hw.EdidSize = 128;
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (hw.ModeCount == 1 && hw.Modes[0]->Width == 640);
  hw.Edid[71] = 0x1e; // Wrong sync polarity.
  checksum (hw.Edid);
  DisplayParseEdid (&hw);
  assert (!hw.ModeCount);
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
