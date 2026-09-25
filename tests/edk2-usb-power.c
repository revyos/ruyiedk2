// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exercise the real board initialization and USB role switches with traced
// MMIO.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/Th1520BoardSupportLib.h>
#include <libfdt.h>
#include <stdlib.h>
#include <string.h>
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
static struct
{
  UINTN Address;
  UINT32 Value;
} Registers[128];
static unsigned Count, Writes, VbusWrites, HighAt, OutputAt, MuxAt;
static BOOLEAN Powered;

static UINT32 *
Register (UINTN Address)
{
  for (unsigned I = 0; I < Count; I++)
    if (Registers[I].Address == Address)
      return &Registers[I].Value;
  assert (Count < ARRAY_SIZE (Registers));
  Registers[Count].Address = Address;
  return &Registers[Count++].Value;
}
UINT32 EFIAPI
MmioWrite32 (UINTN Address, UINT32 Value)
{
  // Only clock gates may change; reject all PLL/divider writes.
  assert (Address < 0xffef010000ULL || Address >= 0xffef011000ULL ||
          Address == 0xffef010204ULL);
  Writes++;
#ifndef TEST_LPI4A
  if (Address == VBUS_DATA)
    assert (*Register (0xffef010204ULL) & BIT8);
  if (Address == ROUTE_DATA)
    assert (*Register (0xffef010204ULL) & BIT6);
#endif
  if (Address == VBUS_DATA || Address == VBUS_DIR || Address == VBUS_PAD ||
      Address == VBUS_MUX)
    VbusWrites++;
  if (Address == VBUS_DATA)
  {
    assert (Value & VBUS_BIT);
    if (!HighAt)
      HighAt = Writes;
  }
  if (Address == VBUS_DIR && (Value & VBUS_BIT))
  {
    assert (*Register (VBUS_DATA) & VBUS_BIT);
    if (!OutputAt)
      OutputAt = Writes;
  }
  if (Address == VBUS_MUX &&
      (Value & MUX_MASK) == (MUX_VALUE << MUX_SHIFT))
  {
    assert (*Register (VBUS_DATA) & *Register (VBUS_DIR) & VBUS_BIT);
    if (!MuxAt)
      MuxAt = Writes;
    Powered = TRUE;
  }
#ifdef TEST_LPI4A
  if (Address == ROUTE_MUX)
  {
    assert ((*Register (ROUTE_DIR) & ROUTE_BIT) != 0);
    assert ((Value & ROUTE_MUX_MASK) == (3U << 20));
  }
#endif
  *Register (Address) = Value;
  if (Powered)
    assert (*Register (VBUS_DATA) & *Register (VBUS_DIR) & VBUS_BIT);
  return Value;
}
UINT32 EFIAPI
MmioRead32 (UINTN Address)
{
  return *Register (Address);
}
UINT32 EFIAPI
MmioOr32 (UINTN Address, UINT32 Value)
{
  return MmioWrite32 (Address, *Register (Address) | Value);
}
UINT32 EFIAPI
MmioAndThenOr32 (UINTN Address, UINT32 And, UINT32 Or)
{
  return MmioWrite32 (Address, (*Register (Address) & And) | Or);
}
VOID EFIAPI
MemoryFence (VOID)
{
}
UINTN EFIAPI
MicroSecondDelay (UINTN Us)
{
  return Us;
}

static unsigned HubCalls;
static BOOLEAN FailHub;
EFI_STATUS
__wrap_Th1520Pca9557Output (UINTN I2c, UINT8 Address, UINT8 Mask,
                            UINT8 Value)
{
  assert (I2c == 0xffec014000ULL && Address == 0x18 && Mask == BIT3 &&
          Value == BIT3);
  HubCalls++;
  return FailHub ? EFI_TIMEOUT : EFI_SUCCESS;
}

static int
SupplyNode (void *Fdt)
{
  int Node = fdt_node_offset_by_compatible (Fdt, -1, "usb-a-connector");
  const fdt32_t *P = fdt_getprop (Fdt, Node, "vbus-supply", NULL);
  assert (P);
  return fdt_node_offset_by_phandle (Fdt, fdt32_to_cpu (*P));
}

static void
CheckErrors (const void *Original)
{
  size_t Size = fdt_totalsize (Original);
  void *Fdt = malloc (Size);
  assert (Fdt);
  for (unsigned Case = 0; Case < 7; Case++)
  {
    memcpy (Fdt, Original, Size);
    int Fan = fdt_node_offset_by_compatible (Fdt, -1, "pwm-fan");
    assert (!fdt_setprop_string (Fdt, Fan, "status", "disabled"));
    int Reg = SupplyNode (Fdt);
    int Length;
    const fdt32_t *P = fdt_getprop (Fdt, Reg, "gpio", &Length);
    assert (P && Length == 12);
    fdt32_t Gpio[3];
    memcpy (Gpio, P, sizeof (Gpio));
    if (Case == 0)
      Gpio[0] = cpu_to_fdt32 (0xffffffff);
    if (Case == 1)
      Gpio[1] = cpu_to_fdt32 (32);
    if (Case == 2)
      Gpio[2] = cpu_to_fdt32 (2);
    if (Case <= 2)
      assert (!fdt_setprop (Fdt, Reg, "gpio", Gpio, sizeof (Gpio)));
    if (Case == 3)
    {
      int Port = fdt_node_offset_by_phandle (Fdt, fdt32_to_cpu (*P));
      assert (!fdt_setprop_string (Fdt, fdt_parent_offset (Fdt, Port),
                                   "status", "disabled"));
    }
    if (Case == 4)
      assert (!fdt_setprop_u32 (Fdt, Reg, "vin-supply",
                                fdt_get_phandle (Fdt, Reg)));
    if (Case == 5)
      assert (!fdt_setprop_u32 (Fdt, Reg, "pinctrl-0", 0xffffffff));
    if (Case == 6)
    {
      int Usb = fdt_node_offset_by_compatible (Fdt, -1, "snps,dwc3");
      assert (!fdt_setprop_string (Fdt, Usb, "status", "disabled"));
    }
    unsigned Before = Writes;
    EFI_STATUS Status = Th1520FdtEarlyInit (Fdt);
    assert (Case == 6 ? Status == EFI_SUCCESS : EFI_ERROR (Status));
    assert (Writes == Before);
  }
  // An invalid PHY reference must fail before applying any hub supplies.
  memcpy (Fdt, Original, Size);
  int Usb = fdt_node_offset_by_compatible (Fdt, -1, "snps,dwc3");
  assert (!fdt_setprop_u32 (Fdt, Usb, "phys", 0xffffffff));
  unsigned Before = Writes, Calls = HubCalls;
  UINTN Base;
  assert (EFI_ERROR (Th1520FdtUsbHostInit (Fdt, &Base)));
  assert (Writes == Before && HubCalls == Calls);

  // A malformed pin group must not leave earlier pins partly applied.
  memcpy (Fdt, Original, Size);
  int Reg = SupplyNode (Fdt);
  const fdt32_t *P = fdt_getprop (Fdt, Reg, "pinctrl-0", NULL);
  int Pins = fdt_node_offset_by_phandle (Fdt, fdt32_to_cpu (*P));
  int Child = fdt_first_subnode (Fdt, Pins);
  const char *Name = fdt_getprop (Fdt, Child, "pins", NULL);
  char Names[64];
  size_t Length = strlen (Name) + 1;
  memcpy (Names, Name, Length);
  memcpy (Names + Length, "invalid", sizeof ("invalid"));
  assert (!fdt_setprop (Fdt, Child, "pins", Names,
                        Length + sizeof ("invalid")));
  assert (EFI_ERROR (Th1520FdtPinctrl (Fdt, SupplyNode (Fdt))));
  assert (Writes == Before);
  free (Fdt);
}

static void
CheckFan (const void *Original)
{
  size_t Size = fdt_totalsize (Original);
  void *Fdt = malloc (Size);
  assert (Fdt);
  memcpy (Fdt, Original, Size);
  int Usb = fdt_node_offset_by_compatible (Fdt, -1, "snps,dwc3");
  assert (!fdt_setprop_string (Fdt, Usb, "status", "disabled"));
  int Fan = fdt_node_offset_by_compatible (Fdt, -1, "pwm-fan");
  const fdt32_t *P = fdt_getprop (Fdt, Fan, "pwms", NULL);
  fdt32_t Pwm[] = {*P, cpu_to_fdt32 (3), cpu_to_fdt32 (5000000),
                   cpu_to_fdt32 (1)};
  assert (!fdt_setprop (Fdt, Fan, "pwms", Pwm, sizeof (Pwm)));
  assert (Th1520FdtEarlyInit (Fdt) == EFI_SUCCESS);
  assert (*Register (0xffec01c068ULL) == 500000);
  assert (*Register (0xffec01c06cULL) == 500000);
  assert (*Register (0xffec01c060ULL) == (BIT5 | BIT0));
  // The inherited divider changes the period count, never the clock.
  *Register (0xffef010150ULL) = 7;
  assert (Th1520FdtEarlyInit (Fdt) == EFI_SUCCESS);
  assert (*Register (0xffec01c068ULL) == 250000);
  Pwm[1] = cpu_to_fdt32 (6);
  assert (!fdt_setprop (Fdt, Fan, "pwms", Pwm, sizeof (Pwm)));
  unsigned Before = Writes;
  assert (EFI_ERROR (Th1520FdtEarlyInit (Fdt)));
  assert (Writes == Before);
  free (Fdt);
}

int
main (int Argc, char **Argv)
{
  assert (Argc == 2);
  FILE *File = fopen (Argv[1], "rb");
  assert (File);
  fseek (File, 0, SEEK_END);
  long Length = ftell (File);
  rewind (File);
  void *Fdt = malloc (Length + 8192);
  assert (Fdt && fread (Fdt, 1, Length, File) == (size_t)Length);
  fclose (File);
  assert (!fdt_open_into (Fdt, Fdt, Length + 8192));
  // Inherited 2 GHz GMAC PLL / 5 / 4 = 100 MHz PWM clock.
  *Register (0xffef010020ULL) = 3 | (250 << 8) | BIT20 | BIT24;
  *Register (0xffef010024ULL) = BIT24;
  *Register (0xffef010140ULL) = 5;
  *Register (0xffef010150ULL) = 3;
  // Cold initialization: VBUS starts low, input and disconnected from
  // GPIO.
  *Register (VBUS_MUX) = 0xffffffff;
  *Register (VBUS_PAD) = 0x01230123;
  *Register (VBUS_DATA) = BIT4;
  *Register (VBUS_DIR) = BIT5;
#ifdef TEST_LPI4A
  // AON GPIO4 is pad 13 (CPU_JTG_TDI), which resets to JTAG function 0.
  *Register (ROUTE_MUX) = 0xa0000303;
  *Register (ROUTE_PAD) = 0x02080404;
#endif
  assert (Th1520FdtEarlyInit (Fdt) == EFI_SUCCESS);
  assert (Powered && HighAt < OutputAt && OutputAt < MuxAt);
  assert (((*Register (VBUS_PAD) >> PAD_SHIFT) & 0xffff) ==
          0x104); // Preserve the unspecified slew rate.
  assert (((*Register (VBUS_PAD) >> (16 - PAD_SHIFT)) & 0xffff) == 0x123);
  assert ((*Register (VBUS_MUX) & ~MUX_MASK) == (0xffffffffU & ~MUX_MASK));
  assert (*Register (VBUS_DATA) == (BIT4 | VBUS_BIT));
  assert (*Register (VBUS_DIR) == (BIT5 | VBUS_BIT));
  UINTN Usb;
  assert (Th1520FdtUsbHostInit (Fdt, &Usb) == EFI_SUCCESS);
  assert (Usb == 0xffe7040000ULL);
#ifdef TEST_LPI4A
  assert (HubCalls == 1); // Both peer hubs share a rail.
  UINTN Pwm = 0xffec01c020ULL;
  assert (*Register (0xffef010204ULL) & BIT7);
#else
  assert (HubCalls == 0);
  UINTN Pwm = 0xffec01c040ULL;
  assert (*Register (0xffef010204ULL) & BIT8);
#endif
  assert (*Register (Pwm + 8) == 1000000);
  assert (*Register (Pwm + 12) == 1000000);
  assert (*Register (Pwm) == (BIT8 | BIT5 | BIT0));
  unsigned Before = VbusWrites, Calls = HubCalls;
  for (unsigned I = 0; I < 4; I++)
  {
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
  assert (HubCalls == Calls);
#ifdef TEST_LPI4A
  FailHub = TRUE;
  assert (Th1520FdtUsbHostInit (Fdt, &Usb) == EFI_TIMEOUT);
  FailHub = FALSE;
#endif
  assert (VbusWrites == Before && Powered);
  CheckErrors (Fdt);
  CheckFan (Fdt);
  free (Fdt);
  puts ("TH1520 DT: fan, supply sharing and USB power sequencing passed");
}
