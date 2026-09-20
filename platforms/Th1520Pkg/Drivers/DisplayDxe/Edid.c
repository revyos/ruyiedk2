// SPDX-License-Identifier: GPL-2.0-only
#include "Display.h"

// Nominal EDID timings. Keep the firmware-provided PLL configuration and
// round the pixel divider as the Linux one-based divider driver does.
CONST DISPLAY_TIMING DisplayTimings[] = {
    {1920, 1080, 148500000, 88, 44, 148, 4, 5, 36, 16, TRUE},
    {1280, 720, 74250000, 110, 40, 220, 5, 5, 20, 4, TRUE},
    {800, 600, 40000000, 40, 128, 88, 1, 4, 23, 0, TRUE},
    {640, 480, 25175000, 16, 96, 48, 10, 2, 33, 1, FALSE}};

BOOLEAN
DisplayEdidValid (CONST UINT8 *Data)
{
  UINT8 Sum = 0;
  UINTN Index;
  for (Index = 0; Index < 128; Index++)
  {
    Sum = (UINT8)(Sum + Data[Index]);
  }
  return Sum == 0;
}

STATIC VOID MatchDtd (CONST UINT8 *Dtd, BOOLEAN *Supported)
{
  UINTN Index;
  UINT32 Clock = (Dtd[0] | (Dtd[1] << 8)) * 10000U;
  UINT32 Width = Dtd[2] | ((Dtd[4] & 0xf0) << 4);
  UINT32 Height = Dtd[5] | ((Dtd[7] & 0xf0) << 4);
  UINT32 HBlank = Dtd[3] | ((Dtd[4] & 0xf) << 8);
  UINT32 VBlank = Dtd[6] | ((Dtd[7] & 0xf) << 8);
  UINT32 HFront = Dtd[8] | ((Dtd[11] & 0xc0) << 2);
  UINT32 HSync = Dtd[9] | ((Dtd[11] & 0x30) << 4);
  UINT32 VFront = (Dtd[10] >> 4) | ((Dtd[11] & 0xc) << 2);
  UINT32 VSync = (Dtd[10] & 0xf) | ((Dtd[11] & 3) << 4);
  if ((Dtd[17] & 0x98) != 0x18)
  {
    return; // Progressive, separate H/V sync only.
  }
  for (Index = 0; Index < DISPLAY_MAX_MODES; Index++)
  {
    CONST DISPLAY_TIMING *T = &DisplayTimings[Index];
    // EDID DTD clocks are quantized to 10 kHz (VGA is 25.175 MHz).
    if (Clock + 5000 >= T->Clock && Clock <= T->Clock + 5000 &&
        (Dtd[17] & 6) == (T->PositiveSync ? 6 : 0) &&
        Width == T->Width && Height == T->Height &&
        HBlank == (UINT32)T->HFront + T->HSync + T->HBack &&
        VBlank == (UINT32)T->VFront + T->VSync + T->VBack && HFront == T->HFront &&
        HSync == T->HSync && VFront == T->VFront && VSync == T->VSync)
    {
      Supported[Index] = TRUE;
    }
  }
}

VOID DisplayParseEdid (DISPLAY_HW *Hw)
{
  STATIC CONST UINT8 Header[] = {0, 255, 255, 255, 255, 255, 255, 0};
  BOOLEAN Supported[DISPLAY_MAX_MODES] = {FALSE};
  CONST UINT8 *Cta = Hw->Edid + 128;
  UINTN Index, Pos, End, Length, Tag, Item;
  Hw->ModeCount = 0;
  Hw->HdmiSink = FALSE;
  if (Hw->EdidSize < 128 || !DisplayEdidValid (Hw->Edid) ||
      CompareMem (Hw->Edid, Header, sizeof (Header)) != 0)
  {
    Hw->EdidSize = 0;
    return;
  }
  for (Pos = 54; Pos + 18 <= 126; Pos += 18)
  {
    MatchDtd (Hw->Edid + Pos, Supported);
  }
  // EDID established timings: 800x600@60 and 640x480@60.
  Supported[2] |= (Hw->Edid[35] & BIT0) != 0;
  Supported[3] |= (Hw->Edid[35] & BIT5) != 0;
  for (Pos = 38; Pos < 54; Pos += 2)
  {
    // Standard timing, 4:3 aspect ratio, 60 Hz.
    if (Hw->Edid[Pos + 1] == 0x40)
    {
      for (Index = 2; Index < DISPLAY_MAX_MODES; Index++)
      {
        if (((UINT32)Hw->Edid[Pos] + 31) * 8 == DisplayTimings[Index].Width)
        {
          Supported[Index] = TRUE;
        }
      }
    }
  }
  if (Hw->EdidSize == 256 && Hw->Edid[126] != 0 && DisplayEdidValid (Cta) && Cta[0] == 2)
  {
    End = Cta[2];
    if (End >= 4 && End <= 127)
    {
      for (Pos = 4; Pos < End; Pos += Length + 1)
      {
        Length = Cta[Pos] & 31;
        Tag = Cta[Pos] >> 5;
        if (Length > End - Pos - 1)
        {
          break;
        }
        if (Tag == 2)
        {
          for (Item = 1; Item <= Length; Item++)
          {
            for (Index = 0; Index < DISPLAY_MAX_MODES; Index++)
            {
              if (DisplayTimings[Index].Vic != 0 &&
                  (Cta[Pos + Item] & 127) == DisplayTimings[Index].Vic)
              {
                Supported[Index] = TRUE;
              }
            }
          }
        }
        if (Tag == 3 && Length >= 3 && Cta[Pos + 1] == 3 && Cta[Pos + 2] == 12 &&
            Cta[Pos + 3] == 0)
        {
          Hw->HdmiSink = TRUE;
        }
      }
      for (Pos = End; Pos + 18 <= 127; Pos += 18)
      {
        MatchDtd (Cta + Pos, Supported);
      }
    }
  }
  for (Index = 0; Index < DISPLAY_MAX_MODES; Index++)
  {
    if (Supported[Index])
    {
      Hw->Modes[Hw->ModeCount++] = &DisplayTimings[Index];
    }
  }
}
