// SPDX-License-Identifier: BSD-2-Clause-Patent
#define MDEPKG_NDEBUG
#define _PCD_GET_MODE_BOOL_PcdFirmwareOnEmmc 0
#define _PCD_GET_MODE_BOOL_PcdRecoveryMode 0
#define _PCD_GET_MODE_32_PcdFirmwareImageLimit 0x400000
#include "../platforms/Th1520Pkg/Drivers/EmmcDxe/EmmcDxe.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
EFI_GUID gFdtClientProtocolGuid;
static const char *Capability;
static UINT32 Frequency;
static BOOLEAN HasFrequency, Disabled, Removable, No18;
static UINT32 Width = 8, RegSize = 16;
UINT32 EFIAPI SwapBytes32 (UINT32 V) { return __builtin_bswap32 (V); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
UINT64 EFIAPI SwapBytes64 (UINT64 V) { return __builtin_bswap64 (V); }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P) { UINT32 V; memcpy (&V,P,4); return V; }
UINT64 EFIAPI ReadUnaligned64 (CONST VOID *P) { UINT64 V; memcpy (&V,P,8); return V; }
static EFI_STATUS EFIAPI
Next (FDT_CLIENT_PROTOCOL *F, CONST CHAR8 *C, INT32 Prev, INT32 *Node)
{
  if (Prev >= 0) { return EFI_NOT_FOUND; }
  *Node = 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Property (FDT_CLIENT_PROTOCOL *F, INT32 N, CONST CHAR8 *Name,
          CONST VOID **Value, UINT32 *Size)
{
  static const UINT8 Reg[16] = {0,0,0,255,231,8,0,0,0,0,0,0,0,1,0,0};
  static UINT32 Bus;
  if (!strcmp (Name, "status") && Disabled)
  {
    *Value = "disabled"; *Size = 9; return EFI_SUCCESS;
  }
  if ((!strcmp (Name, "non-removable") && !Removable) ||
      (!strcmp (Name, "no-1-8-v") && No18))
  {
    *Value = Reg; *Size = 0; return EFI_SUCCESS;
  }
  if (!strcmp (Name, "bus-width") && Width)
  {
    Bus = SwapBytes32 (Width);
    *Value = &Bus; *Size = 4; return EFI_SUCCESS;
  }
  if (!strcmp (Name, "reg"))
  {
    *Value = Reg; *Size = RegSize; return EFI_SUCCESS;
  }
  if (Capability != NULL && !strcmp (Name, Capability))
  {
    *Value = Reg; *Size = 0; return EFI_SUCCESS;
  }
  if (HasFrequency && !strcmp (Name, "max-frequency"))
  {
    *Value = &Frequency; *Size = 4; return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}
static FDT_CLIENT_PROTOCOL Fdt = {
    .FindNextCompatibleNode = Next, .GetNodeProperty = Property};
static EFI_STATUS EFIAPI
Locate (EFI_GUID *Guid, VOID *Registration, VOID **Protocol)
{
  *Protocol = &Fdt; return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES Services = {.LocateProtocol = Locate};
EFI_BOOT_SERVICES *gBS = &Services;
int main (void)
{
  assert (ParseHardware () == EFI_SUCCESS);
  assert (MMC == 0xffe7080000ULL && BusWidth == 8);
  assert (RateLimit == 25000000);
  Capability = "mmc-hs200-1_8v";
  assert (ParseHardware () == EFI_SUCCESS && RateLimit == 52000000);
  Capability = "mmc-hs400-1_8v";
  assert (ParseHardware () == EFI_SUCCESS && RateLimit == 52000000);
  HasFrequency = TRUE;
  Frequency = SwapBytes32 (200000000);
  assert (ParseHardware () == EFI_SUCCESS && RateLimit == 52000000);
  Frequency = SwapBytes32 (24000000);
  assert (ParseHardware () == EFI_SUCCESS && RateLimit == 24000000);
  for (Width = 0; Width <= 8; Width += 4)
  {
    assert (ParseHardware () == EFI_SUCCESS);
    assert (BusWidth == (Width ? Width : 1));
  }
  Width = 3;
  assert (ParseHardware () == EFI_UNSUPPORTED);
  Width = 8;
  Disabled = TRUE;
  assert (ParseHardware () == EFI_NOT_FOUND);
  Disabled = FALSE;
  Removable = TRUE;
  assert (ParseHardware () == EFI_NOT_FOUND);
  Removable = FALSE;
  No18 = TRUE;
  assert (ParseHardware () == EFI_UNSUPPORTED);
  No18 = FALSE;
  RegSize = 8;
  assert (ParseHardware () == EFI_COMPROMISED_DATA);
  RegSize = 16;
  Frequency = 0;
  assert (ParseHardware () == EFI_UNSUPPORTED);
  puts ("eMMC DT: controller, width, status and HS52 limits passed");
}
