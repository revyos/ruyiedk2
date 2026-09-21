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
static BOOLEAN HasFrequency;
UINT32 EFIAPI SwapBytes32 (UINT32 V) { return __builtin_bswap32 (V); }
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
  static const UINT8 Reg[16] = {0,0,0,255,231,8,0,0};
  if (!strcmp (Name, "reg"))
  {
    *Value = Reg; *Size = sizeof (Reg); return EFI_SUCCESS;
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
  assert (MaximumRate () == 25000000);
  HasFrequency = TRUE;
  Frequency = SwapBytes32 (12000000);
  assert (MaximumRate () == 12000000);
  Capability = "cap-mmc-highspeed";
  Frequency = SwapBytes32 (52000000);
  assert (MaximumRate () == 52000000);
  Capability = "mmc-hs400-1_8v";
  Frequency = SwapBytes32 (200000000);
  assert (MaximumRate () == 52000000);
  Capability = "mmc-hs200-1_8v";
  assert (MaximumRate () == 52000000);
  Frequency = SwapBytes32 (24000000);
  assert (MaximumRate () == 24000000);
  puts ("eMMC DT: legacy limits and HS200/HS400 HS52 fallback passed");
}
