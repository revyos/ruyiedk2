// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Gmac.h"
#include <Library/Th1520MacStoreLib.h>

typedef struct {
  BOOLEAN Present, DtValid, OverrideValid, FuseValid;
  TH1520_MAC_SOURCE Source;
  UINT8 Dt[6], Override[6], Fuse[6], Mac[6];
  CONST CHAR8 *Reason;
  EFI_STATUS Status;
} MAC_CHOICE;
STATIC MAC_CHOICE Choices[2];
STATIC BOOLEAN Prepared;

STATIC INT32
Find (FDT_CLIENT_PROTOCOL *Fdt, UINTN Index)
{
  INT32 Node = -1;
  CONST VOID *P;
  UINT32 Size;
  UINTN Base = Index ? 0xffe7060000ULL : 0xffe7070000ULL;
  while (!EFI_ERROR (Fdt->FindNextCompatibleNode (
      Fdt, "thead,th1520-gmac", Node, &Node)))
  {
    if (!EFI_ERROR (Fdt->GetNodeProperty (
        Fdt, Node, "status", &P, &Size)) &&
        !((Size == 5 && !CompareMem (P, "okay", 5)) ||
          (Size == 3 && !CompareMem (P, "ok", 3))))
      continue;
    if (!EFI_ERROR (Fdt->GetNodeProperty (Fdt, Node, "reg", &P, &Size)) &&
        Size == 32 && SwapBytes64 (ReadUnaligned64 (P)) == Base)
      return Node;
  }
  return -1;
}

STATIC BOOLEAN
DtAddress (FDT_CLIENT_PROTOCOL *Fdt, INT32 Node, UINT8 *Mac)
{
  STATIC CONST CHAR8 *Names[] = {"mac-address", "local-mac-address"};
  CONST VOID *P;
  UINT32 Size;
  for (UINTN I = 0; I < ARRAY_SIZE (Names); I++)
    if (!EFI_ERROR (Fdt->GetNodeProperty (
        Fdt, Node, Names[I], &P, &Size)) &&
        Size == 6 && Th1520MacValid (P))
    {
      CopyMem (Mac, P, 6);
      return TRUE;
    }
  return FALSE;
}

EFI_STATUS
GmacPrepareAddresses (FDT_CLIENT_PROTOCOL *Fdt)
{
  STATIC CONST UINT8 Zero[6];
  TH1520_MAC_SETTINGS Settings;
  if (Prepared)
    return EFI_SUCCESS;
  GmacReadSettings (&Settings);
  // Collect all candidates before patching either node.
  for (UINTN I = 0; I < 2; I++)
  {
    MAC_CHOICE *C = &Choices[I];
    GMAC_HARDWARE Hw = {.Index = I};
    INT32 Node = Find (Fdt, I);
    C->Status = EFI_NOT_FOUND;
    C->Present = Node >= 0;
    if (!C->Present)
      continue;
    C->DtValid = DtAddress (Fdt, Node, C->Dt);
    C->OverrideValid = (Settings.OverrideMask & (1U << I)) &&
        Th1520MacValid (Settings.Address[I]);
    CopyMem (C->Override, Settings.Address[I], 6);
    if (C->DtValid || C->OverrideValid)
      continue;
    EFI_STATUS Status = GmacReadFactoryAddress (&Hw, C->Fuse);
    C->FuseValid = !EFI_ERROR (Status) && Th1520MacValid (C->Fuse);
    C->Reason = EFI_ERROR (Status) ? "unavailable" :
        !CompareMem (C->Fuse, Zero, 6) ? "empty" : "invalid";
  }
  if (!Choices[0].Present && !Choices[1].Present)
  {
    Prepared = TRUE;
    return EFI_SUCCESS;
  }
  for (UINTN I = 0; I < 2; I++)
  {
    MAC_CHOICE *C = &Choices[I], *Peer = &Choices[1 - I];
    if (C->DtValid)
    {
      // Explicit DT assignments need not be adjacent.
      C->Source = Th1520MacDeviceTree;
      CopyMem (C->Mac, C->Dt, 6);
      C->Status = EFI_SUCCESS;
    }
    else if (C->OverrideValid)
    {
      C->Source = Th1520MacConfigured;
      CopyMem (C->Mac, C->Override, 6);
      C->Status = EFI_SUCCESS;
    }
    else if (C->FuseValid)
    {
      BOOLEAN Duplicate =
          (Peer->DtValid && !CompareMem (C->Fuse, Peer->Dt, 6)) ||
          (!Peer->DtValid && Peer->OverrideValid &&
           !CompareMem (C->Fuse, Peer->Override, 6)) ||
          (I == 1 && Peer->FuseValid &&
           !CompareMem (C->Fuse, Peer->Fuse, 6));
      if (!Duplicate)
      {
        C->Source = Th1520MacFactory;
        CopyMem (C->Mac, C->Fuse, 6);
        C->Status = EFI_SUCCESS;
      }
      else
        C->Reason = "duplicate";
    }
  }
  if (EFI_ERROR (Choices[0].Status) &&
      (Choices[0].Present || EFI_ERROR (Choices[1].Status)))
  {
    Choices[0].Source = Th1520MacFallback;
    Choices[0].Status = GmacFallbackAddress (0, Choices[0].Mac);
    if (!EFI_ERROR (Choices[0].Status) &&
        !EFI_ERROR (Choices[1].Status) &&
        !CompareMem (Choices[0].Mac, Choices[1].Mac, 6))
    {
      UINT8 Base[6];
      CopyMem (Base, Choices[0].Mac, 6);
      // Preserve the explicitly assigned peer address.
      Th1520MacAdjacent (Base, Choices[0].Mac);
    }
  }
  if (Choices[1].Present && EFI_ERROR (Choices[1].Status))
  {
    Choices[1].Source = Th1520MacFallback;
    Choices[1].Status = EFI_ERROR (Choices[0].Status) ? Choices[0].Status :
        Th1520MacAdjacent (Choices[0].Mac, Choices[1].Mac);
  }
  Prepared = TRUE;
  for (UINTN I = 0; I < 2; I++)
  {
    MAC_CHOICE *C = &Choices[I];
    if (!C->Present || EFI_ERROR (C->Status))
      continue;
    INT32 Node = Find (Fdt, I);
    if (Node < 0)
    {
      C->Status = EFI_NOT_FOUND;
      continue;
    }
    if (C->Source == Th1520MacFallback)
      C->Status = Fdt->SetNodeProperty (Fdt, Node,
          "thead,random-mac-reason", C->Reason,
          (UINT32)AsciiStrSize (C->Reason));
    if (!EFI_ERROR (C->Status))
      C->Status = Fdt->SetNodeProperty (
          Fdt, Node, "mac-address", C->Mac, 6);
    if (!EFI_ERROR (C->Status))
      C->Status = Fdt->SetNodeProperty (Fdt, Node, "local-mac-address",
                                       C->Mac, 6);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
GmacSelectAddress (FDT_CLIENT_PROTOCOL *Fdt, GMAC_HARDWARE *Hw, UINT8 *Mac)
{
  if (Hw->Index > 1)
    return EFI_UNSUPPORTED;
  if (!Prepared)
    GmacPrepareAddresses (Fdt);
  if (!EFI_ERROR (Choices[Hw->Index].Status))
    CopyMem (Mac, Choices[Hw->Index].Mac, 6);
  return Choices[Hw->Index].Status;
}

EFI_STATUS EFIAPI
GmacGetConfig (TH1520_MAC_SETTINGS *Settings, TH1520_MAC_INFO Info[2])
{
  if (!Settings || !Info)
    return EFI_INVALID_PARAMETER;
  EFI_STATUS Status = GmacReadSettings (Settings);
  if (Status != EFI_NOT_FOUND && Status != EFI_COMPROMISED_DATA &&
      EFI_ERROR (Status))
    return Status;
  for (UINTN I = 0; I < 2; I++)
  {
    Info[I].Present = Choices[I].Present;
    Info[I].Source = Choices[I].Source;
    CopyMem (Info[I].Address, Choices[I].Mac, 6);
  }
  return EFI_SUCCESS;
}
