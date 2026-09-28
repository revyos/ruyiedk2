// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210PciLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PciSegmentLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UINT8 config[4096];
static EFI_TPL tpl = TPL_APPLICATION;
static unsigned accesses;
static UINT64 prefix = (1ULL << 32) | (17U << 20) | (3U << 12);

static EFI_TPL EFIAPI
raise_tpl (EFI_TPL value)
{
  EFI_TPL previous = tpl;
  assert (value >= tpl);
  tpl = value;
  return previous;
}

static VOID EFIAPI
restore_tpl (EFI_TPL value)
{
  assert (value <= tpl);
  tpl = value;
}

static EFI_BOOT_SERVICES services = {
  .RaiseTPL = raise_tpl, .RestoreTPL = restore_tpl
};
EFI_BOOT_SERVICES *gBS = &services;

VOID * EFIAPI
CopyMem (VOID *dest, CONST VOID *source, UINTN bytes)
{
  return memcpy (dest, source, bytes);
}

UINT32
A210PciRead (UINT64 address, UINTN width)
{
  UINT32 value = 0;
  assert ((address & ~0xfffULL) == prefix);
  assert (!(address & (width - 1)));
  assert ((address & 0xfff) + width <= sizeof (config));
  memcpy (&value, config + (address & 0xfff), width);
  accesses++;
  return value;
}

VOID
A210PciWrite (UINT64 address, UINTN width, UINT32 value)
{
  assert ((address & ~0xfffULL) == prefix);
  assert (!(address & (width - 1)));
  assert ((address & 0xfff) + width <= sizeof (config));
  memcpy (config + (address & 0xfff), &value, width);
  accesses++;
}

int
main (void)
{
  UINT8 input[23], output[23];
  unsigned i, before;
  for (i = 0; i < sizeof (input); i++)
    input[i] = i * 7 + 1;
  memset (config, 0xa5, sizeof (config));
  assert (PciSegmentWriteBuffer (prefix + 0x405, sizeof (input), input)
          == sizeof (input));
  assert (PciSegmentReadBuffer (prefix + 0x405, sizeof (output), output)
          == sizeof (output));
  assert (!memcmp (input, output, sizeof (input)));
  assert (config[0x404] == 0xa5 && config[0x41c] == 0xa5);
  assert (PciSegmentWrite32 (prefix + 0xabc, 0x12345678) == 0x12345678);
  assert (PciSegmentBitFieldRead32 (prefix + 0xabc, 8, 15) == 0x56);
  assert (PciSegmentBitFieldWrite32 (prefix + 0xabc, 8, 15, 0xaa)
          == 0x1234aa78);
  assert (PciSegmentAndThenOr32 (prefix + 0xabc, 0xffffff00, 0x12)
          == 0x1234aa12);
  assert (PciSegmentBitFieldAnd32 (prefix + 0xabc, 8, 15, 0x0f)
          == 0x12340a12);
  assert (PciSegmentBitFieldOr32 (prefix + 0xabc, 8, 15, 0x30)
          == 0x12343a12);
  assert (tpl == TPL_APPLICATION);
  before = accesses;
  assert (!PciSegmentReadBuffer (prefix + 4095, 2, output));
  assert (!PciSegmentWriteBuffer (prefix + 4095, 2, input));
  assert (!PciSegmentReadBuffer (prefix, 0, NULL));
  assert (accesses == before);
  puts ("A210 PCI access checks passed");
  return 0;
}
