// SPDX-License-Identifier: BSD-2-Clause-Patent
#define MDEPKG_NDEBUG
#include "../platforms/Th1520Pkg/Drivers/UsbHostDxe/UsbHost.c"
#include <assert.h>
#include <stdio.h>

static struct { UINTN Address; UINT32 Value; } Regs[32];
static UINTN Count;
static UINT32 BusGate, UsbGate;
EFI_BOOT_SERVICES *gBS;
BOOLEAN UsbDmaIdle (VOID) { assert (FALSE); return FALSE; }
UINT32 EFIAPI MmioRead32 (UINTN Address)
{
  assert (Address != 0xfffc02d104ULL);
  if (Address == 0xffec02c100ULL) return BusGate;
  if (Address == 0xffec02c104ULL) return UsbGate;
  assert ((BusGate & 1) && (UsbGate & 15) == 15);
  if (Address == USB + 0xc120) return 0x5533300b;
  for (UINTN I = 0; I < Count; I++)
    if (Regs[I].Address == Address) return Regs[I].Value;
  return 0;
}
UINT32 EFIAPI MmioWrite32 (UINTN Address, UINT32 Value)
{
  assert (Address != 0xfffc02d104ULL);
  if (Address == 0xffec02c100ULL) BusGate = Value;
  else if (Address == 0xffec02c104ULL)
  {
    assert (BusGate & 1);
    UsbGate = Value;
  }
  else
  {
    assert ((BusGate & 1) && (UsbGate & 15) == 15);
    for (UINTN I = 0; I < Count; I++)
      if (Regs[I].Address == Address)
      {
        Regs[I].Value = Value;
        return Value;
      }
    assert (Count < ARRAY_SIZE (Regs));
    Regs[Count].Address = Address;
    Regs[Count++].Value = Value;
  }
  return Value;
}
UINT32 EFIAPI MmioOr32 (UINTN A, UINT32 V)
{ return MmioWrite32 (A, MmioRead32 (A) | V); }
UINT32 EFIAPI MmioAnd32 (UINTN A, UINT32 V)
{ return MmioWrite32 (A, MmioRead32 (A) & V); }
UINTN EFIAPI MicroSecondDelay (UINTN N) { return N; }
EFI_STATUS EFIAPI Th1520BoardUsbRole (BOOLEAN Device)
{ assert (!Device); return EFI_SUCCESS; }
int main (void)
{
  USB = 0xffe7040000ULL;
  assert (StartHost () == EFI_SUCCESS);
  assert ((MmioRead32 (USB + 0xc110) & (3U << 12)) == (1U << 12));
  assert (MmioRead32 (0xfffc02e0c0ULL) == MAX_UINT32);
  puts ("USB startup: normal APB gates precede IOPMP and controller MMIO");
}
