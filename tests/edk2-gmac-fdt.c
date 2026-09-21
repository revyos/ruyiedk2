// SPDX-License-Identifier: BSD-2-Clause-Patent
// Test GMAC discovery against both board DTBs and malformed input.
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
VOID *EFIAPI
GetFirstGuidHob (CONST EFI_GUID *Guid)
{
  return &Hob;
}
UINT32 EFIAPI
MmioOr32 (UINTN Address, UINT32 Value)
{
  assert (Address == 0xffef010204ULL && Value == BIT22);
  return Value;
}
UINT32 EFIAPI
MmioAndThenOr32 (UINTN Address, UINT32 And, UINT32 Or)
{
  assert (Address >= 0xffec007000ULL && Address < 0xffec008000ULL &&
          !(Address & 3));
  UINTN Index = (Address - 0xffec007000ULL) / 4;
  Pads[Index] = (Pads[Index] & And) | Or;
  return Pads[Index];
}

static void Check (const char *Path, unsigned Expected)
{
  FILE *File = fopen (Path, "rb");
  assert (File);
  fseek (File, 0, SEEK_END);
  long Length = ftell (File);
  rewind (File);
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
  while ((Node = fdt_node_offset_by_compatible (Fdt, Node,
                                                "thead,th1520-gmac")) >= 0)
  {
    const char *Status = fdt_getprop (Fdt, Node, "status", NULL);
    if (Status && strcmp (Status, "okay") && strcmp (Status, "ok"))
      continue;
    assert (Count < 2);
    assert (GmacParseHardware (NULL, Node, &Hw) == EFI_SUCCESS);
    assert (Hw.Index < 2 && Hw.PhyAddress == Hw.Index + 1);
    assert (Hw.MdioBase == 0xffe7070000ULL && Hw.TxDelay && Hw.RxDelay);
    assert (GmacConfigurePins (&Hw) == EFI_SUCCESS);
    Ports[Count++] = Hw;
  }
  assert (Count == Expected);
  if (Expected == 2)
    assert (((Pads[0x40c / 4] >> 24) & 15) == 1);
  assert ((Pads[0x60 / 4] & 0x3ff) == BIT9); // GMAC0_RXD1 is an input.
  assert (GmacFindNode (&Ports[0], &N) == EFI_SUCCESS);
  // Property insertion can move nodes; find ports by hardware identity.
  assert (!fdt_setprop_string (Fdt, N, "test-insert",
                               "relocate subsequent nodes"));
  for (unsigned I = 0; I < Count; I++)
  {
    assert (GmacFindNode (&Ports[I], &N) == EFI_SUCCESS);
    assert (GmacParseHardware (NULL, N, &Hw) == EFI_SUCCESS);
    assert (Hw.Base == Ports[I].Base && Hw.MdioBase == Ports[I].MdioBase);
  }
  assert (GmacFindNode (&Ports[0], &N) == EFI_SUCCESS);
  int Len;
  const fdt32_t *Handle = fdt_getprop (Fdt, N, "phy-handle", &Len);
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
  puts ("GMAC DT discovery: Meles, LPi4A shared MDIO, relocation and "
        "invalid PHY cases passed");
  return 0;
}
