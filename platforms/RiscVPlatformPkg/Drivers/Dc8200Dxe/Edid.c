// SPDX-License-Identifier: GPL-2.0-only
#include "Display.h"

// Nominal EDID timings. Keep the firmware-provided PLL configuration and
// round the pixel divider as the Linux one-based divider driver does.
CONST DISPLAY_TIMING DisplayTimings[] = {
    {1920, 1080, 148500000, 88, 44, 148, 4, 5, 36, 16, TRUE, TRUE},
    {1280, 720, 74250000, 110, 40, 220, 5, 5, 20, 4, TRUE, TRUE},
    {800, 600, 40000000, 40, 128, 88, 1, 4, 23, 0, TRUE, TRUE},
    {640, 480, 25175000, 16, 96, 48, 10, 2, 33, 1, FALSE, FALSE}};

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

STATIC BOOLEAN SameTiming (CONST DISPLAY_TIMING *A,
                            CONST DISPLAY_TIMING *B)
{
  // EDID encodes the pixel clock in 10 kHz units.
  return A->Width == B->Width && A->Height == B->Height &&
         A->Clock + 5000 >= B->Clock && A->Clock <= B->Clock + 5000 &&
         A->HFront == B->HFront && A->HSync == B->HSync &&
         A->HBack == B->HBack && A->VFront == B->VFront &&
         A->VSync == B->VSync && A->VBack == B->VBack &&
         A->HPositiveSync == B->HPositiveSync &&
         A->VPositiveSync == B->VPositiveSync;
}

STATIC UINTN AddMode (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Timing)
{
  UINTN Index;
  for (Index = 0; Index < Hw->ModeCount; Index++)
    if (SameTiming (&Hw->Modes[Index], Timing))
      return Index;
  if (Hw->ModeCount == DISPLAY_MAX_MODES)
    return MAX_UINTN;
  Hw->Modes[Hw->ModeCount] = *Timing;
  return Hw->ModeCount++;
}

STATIC UINTN AddDtd (DISPLAY_HW *Hw, CONST UINT8 *Dtd)
{
  DISPLAY_TIMING T = {0};
  UINT32 HBlank, VBlank;
  UINTN Index;
  // Interlace, composite sync and borders need different programming.
  if ((Dtd[17] & 0x98) != 0x18 || Dtd[15] || Dtd[16])
    return MAX_UINTN;
  T.Clock = (Dtd[0] | (Dtd[1] << 8)) * 10000U;
  T.Width = Dtd[2] | ((Dtd[4] & 0xf0) << 4);
  T.Height = Dtd[5] | ((Dtd[7] & 0xf0) << 4);
  HBlank = Dtd[3] | ((Dtd[4] & 0xf) << 8);
  VBlank = Dtd[6] | ((Dtd[7] & 0xf) << 8);
  T.HFront = Dtd[8] | ((Dtd[11] & 0xc0) << 2);
  T.HSync = Dtd[9] | ((Dtd[11] & 0x30) << 4);
  T.VFront = (Dtd[10] >> 4) | ((Dtd[11] & 0xc) << 2);
  T.VSync = (Dtd[10] & 0xf) | ((Dtd[11] & 3) << 4);
  // Bound the framebuffer, PHY table and HDMI blanking registers.
  if (!T.Width || !T.Height || T.Width > DISPLAY_MAX_WIDTH ||
      T.Height > DISPLAY_MAX_HEIGHT || T.Clock < 25000000 ||
      T.Clock > 148500000 || !T.HSync || !T.VSync ||
      T.HFront + T.HSync > HBlank || T.VFront + T.VSync > VBlank ||
      VBlank > MAX_UINT8)
    return MAX_UINTN;
  T.HBack = HBlank - T.HFront - T.HSync;
  T.VBack = VBlank - T.VFront - T.VSync;
  T.HPositiveSync = (Dtd[17] & BIT1) != 0;
  T.VPositiveSync = (Dtd[17] & BIT2) != 0;
  for (Index = 0; Index < DISPLAY_STANDARD_MODES; Index++)
    if (SameTiming (&T, &DisplayTimings[Index]))
      T.Vic = DisplayTimings[Index].Vic;
  return AddMode (Hw, &T);
}

STATIC UINTN
ParseCta (DISPLAY_HW *Hw, BOOLEAN *Supported, UINT8 *NativeVic)
{
  CONST UINT8 *Cta = Hw->Edid + 128;
  UINTN End, Pos, Length, Tag, Item, Index;
  UINTN Preferred = MAX_UINTN, Added;
  if (Hw->EdidSize != 256 || !Hw->Edid[126] ||
      !DisplayEdidValid (Cta) || Cta[0] != 2)
    return MAX_UINTN;
  End = Cta[2];
  if (End < 4 || End > 127)
    return MAX_UINTN;
  for (Pos = 4; Pos < End; Pos += Length + 1)
  {
    Length = Cta[Pos] & 31;
    Tag = Cta[Pos] >> 5;
    if (Length > End - Pos - 1)
      break;
    if (Tag == 2)
    {
      for (Item = 1; Item <= Length; Item++)
      {
        for (Index = 0; Index < DISPLAY_STANDARD_MODES; Index++)
        {
          if (DisplayTimings[Index].Vic != 0 &&
              (Cta[Pos + Item] & 127) == DisplayTimings[Index].Vic)
          {
            Supported[Index] = TRUE;
            if (!*NativeVic && (Cta[Pos + Item] & BIT7))
              *NativeVic = DisplayTimings[Index].Vic;
          }
        }
      }
    }
    if (Tag == 3 && Length >= 3 && Cta[Pos + 1] == 3 &&
        Cta[Pos + 2] == 12 && Cta[Pos + 3] == 0)
      Hw->HdmiSink = TRUE;
  }
  for (Pos = End; Pos + 18 <= 127; Pos += 18)
  {
    Added = AddDtd (Hw, Cta + Pos);
    if (Preferred == MAX_UINTN && !*NativeVic &&
        (Pos - End) / 18 < (Cta[3] & 15))
      Preferred = Added;
  }
  return Preferred;
}

VOID DisplayParseEdid (DISPLAY_HW *Hw)
{
  STATIC CONST UINT8 Header[] = {0, 255, 255, 255, 255, 255, 255, 0};
  BOOLEAN Supported[DISPLAY_STANDARD_MODES] = {FALSE};
  UINTN Index, Pos;
  UINTN Preferred = MAX_UINTN, Added;
  UINT8 NativeVic = 0;
  Hw->ModeCount = 0;
  Hw->Preferred = FALSE;
  ZeroMem (Hw->Modes, sizeof (Hw->Modes));
  Hw->HdmiSink = FALSE;
  if (Hw->EdidSize < 128 || !DisplayEdidValid (Hw->Edid) ||
      CompareMem (Hw->Edid, Header, sizeof (Header)) != 0)
  {
    Hw->EdidSize = 0;
    return;
  }
  for (Pos = 54; Pos + 18 <= 126; Pos += 18)
  {
    Added = AddDtd (Hw, Hw->Edid + Pos);
    if (Pos == 54 && ((Hw->Edid[24] & BIT1) ||
        (Hw->Edid[18] == 1 && Hw->Edid[19] >= 4)))
      Preferred = Added;
  }
  // EDID established timings: 800x600@60 and 640x480@60.
  Supported[2] = (Hw->Edid[35] & BIT0) != 0;
  Supported[3] = (Hw->Edid[35] & BIT5) != 0;
  for (Pos = 38; Pos < 54; Pos += 2)
  {
    // Standard timing, 4:3 aspect ratio, 60 Hz.
    if (Hw->Edid[Pos + 1] == 0x40)
    {
      for (Index = 2; Index < DISPLAY_STANDARD_MODES; Index++)
      {
        if (((UINT32)Hw->Edid[Pos] + 31) * 8 == DisplayTimings[Index].Width)
        {
          Supported[Index] = TRUE;
        }
      }
    }
  }
  Added = ParseCta (Hw, Supported, &NativeVic);
  if (Preferred == MAX_UINTN)
    Preferred = Added;
  for (Index = 0; Index < DISPLAY_STANDARD_MODES; Index++)
  {
    if (Supported[Index])
    {
      Added = AddMode (Hw, &DisplayTimings[Index]);
      if (Preferred == MAX_UINTN && NativeVic != 0 &&
          NativeVic == DisplayTimings[Index].Vic)
        Preferred = Added;
    }
  }
  if (Preferred != MAX_UINTN)
  {
    DISPLAY_TIMING T = Hw->Modes[Preferred];
    for (Index = Preferred; Index > 0; Index--)
      Hw->Modes[Index] = Hw->Modes[Index - 1];
    Hw->Modes[0] = T;
    Hw->Preferred = TRUE;
  }
}
