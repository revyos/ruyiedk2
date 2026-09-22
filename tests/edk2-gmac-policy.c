// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/Th1520Pkg/Drivers/GmacDxe/Policy.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  UINT8 Dt[8], Local[8], Fuse[6];
  UINT32 DtSize, LocalSize;
  BOOLEAN Present, FuseError;
  unsigned FuseReads, Patches;
} INPUT;
static INPUT Input[2];
static BOOLEAN Reverse;
static unsigned Fallbacks;
static TH1520_MAC_SETTINGS Settings;
static EFI_STATUS SettingsStatus;
EFI_STATUS GmacReadSettings (TH1520_MAC_SETTINGS *Out)
{
  memcpy (Out, &Settings, sizeof (*Out));
  if (EFI_ERROR (SettingsStatus)) memset (Out, 0, sizeof (*Out));
  return SettingsStatus;
}
static const UINT8 Saved[6] = {2, 0x12, 0x34, 0x56, 0x78, 0x20};
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memmove (D, S, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
UINT64 EFIAPI ReadUnaligned64 (CONST VOID *P)
{ UINT64 V; memcpy (&V, P, 8); return V; }
UINT64 EFIAPI SwapBytes64 (UINT64 V) { return __builtin_bswap64 (V); }
UINTN EFIAPI AsciiStrSize (CONST CHAR8 *S) { return strlen (S) + 1; }
EFI_STATUS GmacReadFactoryAddress (GMAC_HARDWARE *Hw, UINT8 *Mac)
{
  INPUT *I = &Input[Hw->Index];
  I->FuseReads++;
  memcpy (Mac, I->Fuse, 6);
  return I->FuseError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
EFI_STATUS GmacFallbackAddress (UINTN Index, UINT8 *Mac)
{
  assert (Index == 0); // GMAC1 must never generate an independent address.
  Fallbacks++;
  memcpy (Mac, Saved, 6);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Next (FDT_CLIENT_PROTOCOL *F, CONST CHAR8 *C, INT32 Previous, INT32 *Node)
{
  int First = Reverse ? 1 : 0, Last = 1 - First;
  *Node = Previous < 0 ? First : Previous == First ? Last : -1;
  return *Node < 0 ? EFI_NOT_FOUND : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Get (FDT_CLIENT_PROTOCOL *F, INT32 Node, CONST CHAR8 *Name,
     CONST VOID **Data, UINT32 *Size)
{
  static UINT64 Reg[4];
  INPUT *I = &Input[Node];
  if (!strcmp (Name, "status"))
  {
    *Data = I->Present ? "okay" : "disabled";
    *Size = I->Present ? 5 : 9;
    return EFI_SUCCESS;
  }
  if (!strcmp (Name, "reg"))
  {
    Reg[0] = SwapBytes64 (Node ? 0xffe7060000ULL : 0xffe7070000ULL);
    *Data = Reg;
    *Size = sizeof (Reg);
    return EFI_SUCCESS;
  }
  if (!strcmp (Name, "mac-address") && I->DtSize)
  {
    *Data = I->Dt;
    *Size = I->DtSize;
    return EFI_SUCCESS;
  }
  if (!strcmp (Name, "local-mac-address") && I->LocalSize)
  {
    *Data = I->Local;
    *Size = I->LocalSize;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI
Set (FDT_CLIENT_PROTOCOL *F, INT32 Node, CONST CHAR8 *Name,
     CONST VOID *Data, UINT32 Size)
{
  INPUT *I = &Input[Node];
  if (!strcmp (Name, "mac-address"))
  {
    assert (Size == 6);
    memcpy (I->Dt, Data, 6);
    I->DtSize = 6;
    I->Patches++;
  }
  else if (!strcmp (Name, "local-mac-address"))
  {
    assert (Size == 6);
    memcpy (I->Local, Data, 6);
    I->LocalSize = 6;
    I->Patches++;
  }
  return EFI_SUCCESS;
}
static FDT_CLIENT_PROTOCOL Fdt = {
  .FindNextCompatibleNode = Next, .GetNodeProperty = Get,
  .SetNodeProperty = Set
};

static void Reset (void)
{
  memset (Input, 0, sizeof (Input));
  memset (Choices, 0, sizeof (Choices));
  Prepared = FALSE;
  Fallbacks = 0;
  memset (&Settings, 0, sizeof (Settings));
  SettingsStatus = EFI_SUCCESS;
  Input[0].Present = Input[1].Present = TRUE;
}
static void ReadBoth (UINT8 Mac[2][6])
{
  // Ask for GMAC1 first as well as reversing the FDT enumeration order.
  for (int I = 1; I >= 0; I--)
  {
    GMAC_HARDWARE Hw = {.Index = I};
    assert (GmacSelectAddress (&Fdt, &Hw, Mac[I]) == EFI_SUCCESS);
    assert (!memcmp (Input[I].Dt, Mac[I], 6));
    assert (!memcmp (Input[I].Local, Mac[I], 6));
    assert (Input[I].Patches == 2);
  }
}
int main (void)
{
  static const UINT8 Invalid[][6] = {
    {0}, {1, 0, 0x5e, 0, 0, 1}, {0x33, 0x33, 0, 0, 0, 1},
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
  };
  const UINT8 Explicit[6] = {0, 0x11, 0x22, 0x33, 0x44, 0xff};
  const UINT8 Factory[6] = {0, 0x21, 0x32, 0x43, 0x54, 0x10};
  const UINT8 Adjacent[6] = {0, 0x11, 0x22, 0x33, 0x45, 0};
  UINT8 Mac[2][6];
  for (unsigned Order = 0; Order < 2; Order++)
  {
    Reverse = Order;
    Reset ();
    memcpy (Input[0].Dt, Explicit, 6);
    Input[0].DtSize = 6;
    memcpy (Input[0].Fuse, Factory, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Explicit, 6));
    assert (!memcmp (Mac[1], Adjacent, 6));
    assert (!Input[0].FuseReads && !Fallbacks);
    Reset ();
    memcpy (Input[0].Fuse, Factory, 6);
    memcpy (Input[1].Dt, Explicit, 6);
    Input[1].DtSize = 6;
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Factory, 6));
    assert (!memcmp (Mac[1], Explicit, 6) && !Fallbacks);
    // Both explicit addresses are deliberate; adjacency is not imposed.
    Reset ();
    for (unsigned I = 0; I < 2; I++)
    {
      memcpy (Input[I].Dt, Explicit, 6);
      Input[I].DtSize = 6;
    }
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Mac[1], 6) && !Fallbacks);
    Reset ();
    Settings.OverrideMask = 1;
    memcpy (Settings.Address[0], Saved, 6);
    memcpy (Input[0].Fuse, Factory, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Saved, 6));
    assert (Mac[1][5] == Saved[5] + 1 && !Fallbacks);
    assert (!Input[0].FuseReads);
    Reset ();
    Settings.OverrideMask = 1;
    memcpy (Settings.Address[0], Saved, 6);
    Input[0].DtSize = 6;
    memcpy (Input[0].Dt, Explicit, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Explicit, 6));
    assert (!memcmp (Mac[1], Adjacent, 6));
    Reset ();
    Settings.OverrideMask = 3;
    memset (Settings.Address[0], 0xff, 6);
    Settings.Address[1][0] = 1;
    memcpy (Input[0].Fuse, Factory, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Factory, 6));
    assert (!Fallbacks);
    for (unsigned Port = 0; Port < 2; Port++)
      for (unsigned Bad = 0; Bad < ARRAY_SIZE (Invalid); Bad++)
      {
        // An invalid higher-priority property must not hide a valid one.
        Reset ();
        memcpy (Input[Port].Dt, Invalid[Bad], 6);
        Input[Port].DtSize = Input[Port].LocalSize = 6;
        memcpy (Input[Port].Local, Explicit, 6);
        ReadBoth (Mac);
        assert (!memcmp (Mac[Port], Explicit, 6));
        assert (!Input[Port].FuseReads);

        // Neither DT property may bypass validation on either port.
        Reset ();
        memcpy (Input[Port].Dt, Invalid[Bad], 6);
        memcpy (Input[Port].Local, Invalid[Bad], 6);
        Input[Port].DtSize = Input[Port].LocalSize = 6;
        memcpy (Input[Port].Fuse, Factory, 6);
        ReadBoth (Mac);
        assert (!memcmp (Mac[Port], Factory, 6));

        // Unprogrammed and invalid factory slots both require fallback.
        Reset ();
        for (unsigned I = 0; I < 2; I++)
          memcpy (Input[I].Fuse, Invalid[Bad], 6);
        ReadBoth (Mac);
        assert (!memcmp (Mac[0], Saved, 6) && Fallbacks == 1);
        assert (!memcmp (Mac[1], Saved, 5));
        assert (Mac[1][5] == Saved[5] + 1);
      }
    for (unsigned Port = 0; Port < 2; Port++)
      for (unsigned Size = 1; Size <= 8; Size++)
      {
        if (Size == 6)
          continue;
        Reset ();
        memcpy (Input[Port].Dt, Explicit, 6);
        memcpy (Input[Port].Local, Explicit, 6);
        Input[Port].DtSize = Input[Port].LocalSize = Size;
        memcpy (Input[Port].Fuse, Factory, 6);
        ReadBoth (Mac);
        assert (!memcmp (Mac[Port], Factory, 6));
      }
    for (unsigned Bad = 0; Bad < 4; Bad++)
    {
      Reset ();
      Input[0].DtSize = Bad == 3 ? 5 : 6;
      if (Bad == 1) Input[0].Dt[0] = 1;
      if (Bad == 2) memset (Input[0].Dt, 0xff, 6);
      memcpy (Input[0].Fuse, Factory, 6);
      ReadBoth (Mac);
      assert (!memcmp (Mac[0], Factory, 6) && !Fallbacks);
    }
    Reset ();
    Input[0].DtSize = 7;
    Input[0].LocalSize = 6;
    memcpy (Input[0].Local, Explicit, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Explicit, 6) && !Input[0].FuseReads);
    Reset ();
    Input[0].Fuse[0] = 1;
    memset (Input[1].Fuse, 0xff, 6);
    ReadBoth (Mac);
    assert (!memcmp (Mac[0], Saved, 6) && Fallbacks == 1);
    assert (Mac[1][5] == Saved[5] + 1);
  }
  UINT8 Edge[6] = {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff};
  assert (Th1520MacAdjacent (Edge, Mac[1]) == EFI_SUCCESS);
  assert (Mac[1][0] == 0xfe && Mac[1][5] == 0xfe);
  Reset ();
  SettingsStatus = EFI_COMPROMISED_DATA;
  ReadBoth (Mac);
  TH1520_MAC_SETTINGS Config;
  TH1520_MAC_INFO Info[2];
  assert (GmacGetConfig (&Config, Info) == EFI_SUCCESS);
  assert (!Config.OverrideMask && Info[0].Present);
  assert (!memcmp (Info[0].Address, Saved, 6));
  SettingsStatus = EFI_DEVICE_ERROR;
  assert (GmacGetConfig (&Config, Info) == EFI_DEVICE_ERROR);
  puts ("MAC policy: invalid DT/eFuse skipped on both ports; "
        "priority and adjacency passed");
}
