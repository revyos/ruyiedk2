// SPDX-License-Identifier: BSD-2-Clause-Patent
// Test the real GMAC DT parser against both board trees and malformed input.
#include <PiDxe.h>
#include <Library/HobLib.h>
#include <Library/FdtLib.h>
#include <Guid/FdtHob.h>
#include "../platforms/Th1520Pkg/Drivers/GmacDxe/Gmac.h"
#include <libfdt.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_GUID gFdtHobGuid;
static struct { EFI_HOB_GUID_TYPE Header; UINT64 Fdt; } Hob;
static UINT32 Pads[1024];
VOID *EFIAPI GetFirstGuidHob (CONST EFI_GUID *Guid) { return &Hob; }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P) { UINT32 V; memcpy (&V, P, 4); return V; }
UINT64 EFIAPI ReadUnaligned64 (CONST VOID *P) { UINT64 V; memcpy (&V, P, 8); return V; }
UINT32 EFIAPI SwapBytes32 (UINT32 V) { return __builtin_bswap32 (V); }
UINT64 EFIAPI SwapBytes64 (UINT64 V) { return __builtin_bswap64 (V); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp (A, B); }
INTN EFIAPI AsciiStrnCmp (CONST CHAR8 *A, CONST CHAR8 *B, UINTN N) { return strncmp (A, B, N); }
UINTN EFIAPI AsciiStrnLenS (CONST CHAR8 *S, UINTN N) { return strnlen (S, N); }
EFI_STATUS EFIAPI AsciiStrDecimalToUintnS (CONST CHAR8 *S, CHAR8 **End, UINTN *Value)
{ *Value = strtoull (S, End, 10); return *End == S ? EFI_INVALID_PARAMETER : EFI_SUCCESS; }
CONST VOID *EFIAPI FdtGetProp (CONST VOID *F, INT32 N, CONST CHAR8 *P, INT32 *L)
{ return fdt_getprop (F, N, P, L); }
INT32 EFIAPI FdtNodeOffsetByCompatible (CONST VOID *F, INT32 N, CONST CHAR8 *S)
{ return fdt_node_offset_by_compatible (F, N, S); }
INT32 EFIAPI FdtNodeOffsetByPhandle (CONST VOID *F, UINT32 P) { return fdt_node_offset_by_phandle (F, P); }
INT32 EFIAPI FdtParentOffset (CONST VOID *F, INT32 N) { return fdt_parent_offset (F, N); }
INT32 EFIAPI FdtFirstSubnode (CONST VOID *F, INT32 N) { return fdt_first_subnode (F, N); }
INT32 EFIAPI FdtNextSubnode (CONST VOID *F, INT32 N) { return fdt_next_subnode (F, N); }
UINT32 EFIAPI MmioAndThenOr32 (UINTN Address, UINT32 And, UINT32 Or)
{
  assert (Address >= 0xffec007000ULL && Address < 0xffec008000ULL && !(Address & 3));
  UINTN Index = (Address - 0xffec007000ULL) / 4;
  Pads[Index] = (Pads[Index] & And) | Or;
  return Pads[Index];
}

static void Check (const char *Path, unsigned Expected)
{
  FILE *File = fopen (Path, "rb");
  assert (File);
  fseek (File, 0, SEEK_END); long Length = ftell (File); rewind (File);
  void *Fdt = malloc (Length + 8192);
  assert (Fdt && fread (Fdt, 1, Length, File) == (size_t)Length);
  fclose (File);
  assert (!fdt_open_into (Fdt, Fdt, Length + 8192));
  Hob.Header.Header.HobLength = sizeof (Hob);
  Hob.Fdt = (UINTN)Fdt;
  GMAC_HARDWARE Ports[2], Hw;
  unsigned Count = 0;
  int Node = -1, N;
  memset (Pads, 0, sizeof (Pads));
  while ((Node = fdt_node_offset_by_compatible (Fdt, Node, "thead,th1520-gmac")) >= 0)
  {
    const char *Status = fdt_getprop (Fdt, Node, "status", NULL);
    if (Status && strcmp (Status, "okay") && strcmp (Status, "ok")) continue;
    assert (Count < 2);
    assert (GmacParseHardware (NULL, Node, &Hw) == EFI_SUCCESS);
    assert (Hw.Index < 2 && Hw.PhyAddress == Hw.Index + 1);
    assert (Hw.MdioBase == 0xffe7070000ULL && Hw.TxDelay && Hw.RxDelay);
    assert (GmacConfigurePins (&Hw) == EFI_SUCCESS);
    Ports[Count++] = Hw;
  }
  assert (Count == Expected);
  if (Expected == 2) assert (((Pads[0x40c / 4] >> 24) & 15) == 1);
  assert ((Pads[0x60 / 4] & 0x3ff) == BIT9); // GMAC0_RXD1 is an input.
  assert (GmacFindNode (&Ports[0], &N) == EFI_SUCCESS);
  // Adding a property can relocate the other port; lookups must use identity.
  assert (!fdt_setprop_string (Fdt, N, "test-insert", "relocate subsequent nodes"));
  for (unsigned I = 0; I < Count; I++) {
    assert (GmacFindNode (&Ports[I], &N) == EFI_SUCCESS);
    assert (GmacParseHardware (NULL, N, &Hw) == EFI_SUCCESS);
    assert (Hw.Base == Ports[I].Base && Hw.MdioBase == Ports[I].MdioBase);
  }
  assert (GmacFindNode (&Ports[0], &N) == EFI_SUCCESS);
  int Len; const fdt32_t *Handle = fdt_getprop (Fdt, N, "phy-handle", &Len);
  int Phy = fdt_node_offset_by_phandle (Fdt, fdt32_to_cpu (*Handle));
  assert (!fdt_setprop_inplace_u32 (Fdt, Phy, "reg", 32));
  assert (GmacParseHardware (NULL, N, &Hw) == EFI_UNSUPPORTED);
  assert (!fdt_setprop_inplace_u32 (Fdt, Phy, "reg", Ports[0].PhyAddress));
  assert (!fdt_setprop_inplace_u32 (Fdt, N, "phy-handle", 0xffffffff));
  assert (GmacParseHardware (NULL, N, &Hw) == EFI_NOT_FOUND);
  free (Fdt);
}
int main (int Argc, char **Argv)
{
  assert (Argc == 3);
  Check (Argv[1], 1);
  Check (Argv[2], 2);
  puts ("GMAC DT discovery: Meles, LPi4A shared MDIO, relocation and invalid PHY cases passed");
  return 0;
}
