// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exercise the real board initialization and USB role switches with traced MMIO.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <assert.h>
#include <stdio.h>

#ifdef TEST_LPI4A
#define VBUS_DATA 0xffec006000ULL
#define VBUS_BIT BIT22
#define VBUS_PAD 0xffe7f3c06cULL
#define PAD_SHIFT 0
#define VBUS_MUX 0xffe7f3c418ULL
#define MUX_SHIFT 24
#define MUX_VALUE 3
#define ROUTE_DATA 0xfffff41000ULL
#define ROUTE_BIT BIT4
#define HOST_ROUTE_HIGH TRUE
#define ROUTE_MUX 0xfffff4a404ULL
#define ROUTE_PAD 0xfffff4a018ULL
#define ROUTE_MUX_MASK (15U << 20)
#else
#define VBUS_DATA 0xffec005000ULL
#define VBUS_BIT BIT27
#define VBUS_PAD 0xffe7f3c034ULL
#define PAD_SHIFT 16
#define VBUS_MUX 0xffe7f3c40cULL
#define MUX_SHIFT 12
#define MUX_VALUE 0
#define ROUTE_DATA 0xffe7f34000ULL
#define ROUTE_BIT BIT30
#define HOST_ROUTE_HIGH FALSE
#endif
#define VBUS_DIR (VBUS_DATA + 4)
#define ROUTE_DIR (ROUTE_DATA + 4)
#define MUX_MASK (15U << MUX_SHIFT)
static struct { UINTN Address; UINT32 Value; } Registers[32];
static unsigned Count, Writes, VbusWrites, HighAt, OutputAt, MuxAt;
static BOOLEAN Powered;

static UINT32 *Register (UINTN Address)
{
  for (unsigned I = 0; I < Count; I++)
    if (Registers[I].Address == Address) return &Registers[I].Value;
  assert (Count < ARRAY_SIZE (Registers));
  Registers[Count].Address = Address;
  return &Registers[Count++].Value;
}
UINT32 EFIAPI MmioWrite32 (UINTN Address, UINT32 Value)
{
  Writes++;
  if (Address == VBUS_DATA || Address == VBUS_DIR || Address == VBUS_PAD ||
      Address == VBUS_MUX) VbusWrites++;
  if (Address == VBUS_DATA) {
    assert (Value & VBUS_BIT);
    if (!HighAt) HighAt = Writes;
  }
  if (Address == VBUS_DIR && (Value & VBUS_BIT)) {
    assert (*Register (VBUS_DATA) & VBUS_BIT);
    if (!OutputAt) OutputAt = Writes;
  }
  if (Address == VBUS_MUX && (Value & MUX_MASK) == (MUX_VALUE << MUX_SHIFT)) {
    assert (*Register (VBUS_DATA) & *Register (VBUS_DIR) & VBUS_BIT);
    if (!MuxAt) MuxAt = Writes;
    Powered = TRUE;
  }
#ifdef TEST_LPI4A
  if (Address == ROUTE_MUX) {
    assert ((*Register (ROUTE_DIR) & ROUTE_BIT) != 0);
    assert ((Value & ROUTE_MUX_MASK) == (3U << 20));
  }
#endif
  *Register (Address) = Value;
  if (Powered) assert (*Register (VBUS_DATA) & *Register (VBUS_DIR) & VBUS_BIT);
  return Value;
}
UINT32 EFIAPI MmioOr32 (UINTN Address, UINT32 Value)
{ return MmioWrite32 (Address, *Register (Address) | Value); }
UINT32 EFIAPI MmioAndThenOr32 (UINTN Address, UINT32 And, UINT32 Or)
{ return MmioWrite32 (Address, (*Register (Address) & And) | Or); }
VOID EFIAPI MemoryFence (VOID) {}
UINTN EFIAPI MicroSecondDelay (UINTN Us) { return Us; }

#ifdef TEST_LPI4A
static unsigned HubCalls;
static BOOLEAN FailHub;
EFI_STATUS __wrap_Th1520Pca9557Output (UINTN I2c, UINT8 Address, UINT8 Mask, UINT8 Value)
{
  assert (I2c == 0xffec014000ULL && Address == 0x18 && Mask == BIT3 && Value == BIT3);
  HubCalls++;
  return FailHub ? EFI_TIMEOUT : EFI_SUCCESS;
}
#endif

int main (void)
{
  // Cold initialization: VBUS starts low, input and disconnected from GPIO.
  *Register (VBUS_MUX) = 0xffffffff;
  *Register (VBUS_PAD) = 0x01230123;
  *Register (VBUS_DATA) = BIT4;
  *Register (VBUS_DIR) = BIT5;
#ifdef TEST_LPI4A
  // AON GPIO4 is pad 13 (CPU_JTG_TDI), which resets to JTAG function 0.
  *Register (ROUTE_MUX) = 0xa0000303;
  *Register (ROUTE_PAD) = 0x02080404;
#endif
  Th1520BoardEarlyInit ();
  assert (Powered && HighAt < OutputAt && OutputAt < MuxAt);
  assert (((*Register (VBUS_PAD) >> PAD_SHIFT) & 0xffff) == 4); // Same 7 mA drive as Linux DT.
  assert (((*Register (VBUS_PAD) >> (16 - PAD_SHIFT)) & 0xffff) == 0x123);
  assert ((*Register (VBUS_MUX) & ~MUX_MASK) == (0xffffffffU & ~MUX_MASK));
  assert (*Register (VBUS_DATA) == (BIT4 | VBUS_BIT));
  assert (*Register (VBUS_DIR) == (BIT5 | VBUS_BIT));
  unsigned Before = VbusWrites;
  for (unsigned I = 0; I < 4; I++) {
    assert (Th1520BoardUsbRole (FALSE) == EFI_SUCCESS);
    assert (!!(*Register (ROUTE_DATA) & ROUTE_BIT) == HOST_ROUTE_HIGH);
    assert (*Register (ROUTE_DIR) & ROUTE_BIT);
    assert (Th1520BoardUsbRole (TRUE) == EFI_SUCCESS);
    assert (!!(*Register (ROUTE_DATA) & ROUTE_BIT) != HOST_ROUTE_HIGH);
#ifdef TEST_LPI4A
    assert ((*Register (ROUTE_MUX) & ROUTE_MUX_MASK) == (3U << 20));
    assert ((*Register (ROUTE_MUX) & ~ROUTE_MUX_MASK) == 0xa0000303);
    assert (*Register (ROUTE_PAD) == 0x00040404);
#endif
  }
  assert (Th1520BoardUsbRole (FALSE) == EFI_SUCCESS);
#ifdef TEST_LPI4A
  unsigned Calls = HubCalls;
  FailHub = TRUE;
  assert (Th1520BoardUsbRole (FALSE) == EFI_TIMEOUT);
  assert (Th1520BoardUsbRole (TRUE) == EFI_SUCCESS);
  assert (HubCalls == Calls + 1);
#endif
  assert (VbusWrites == Before && Powered);
  puts ("TH1520 USB: VBUS latch precedes mux; role changes leave power untouched");
}
