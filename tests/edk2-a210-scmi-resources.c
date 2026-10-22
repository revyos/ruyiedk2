// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210ScmiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/A210Scmi.h>
#include <Protocol/ArmScmiClock2Protocol.h>
#include <Protocol/ArmScmiPerformanceProtocol.h>
#include <libfdt.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_GUID gA210ScmiProtocolGuid = { 1 };
EFI_GUID gArmScmiClock2ProtocolGuid = { 2 };
EFI_GUID gArmScmiPerformanceProtocolGuid = { 3 };
static UINT32 clocks, resets, last_flags;
static EFI_TPL tpl = TPL_APPLICATION;
static BOOLEAN fail_clock, fail_reset;
static EFI_TPL EFIAPI raise_tpl (EFI_TPL next)
{
  EFI_TPL old = tpl;
  assert (next >= old);
  tpl = next;
  return old;
}
static VOID EFIAPI restore_tpl (EFI_TPL old) { tpl = old; }
static EFI_STATUS EFIAPI enable (SCMI_CLOCK2_PROTOCOL *this, UINT32 id, BOOLEAN on)
{
  assert (tpl == TPL_NOTIFY && on && id < 163);
  clocks++;
  return fail_clock && clocks == 2 ? EFI_NOT_FOUND : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI rate_get (SCMI_CLOCK2_PROTOCOL *this, UINT32 id, UINT64 *rate)
{
  assert (tpl == TPL_NOTIFY && id == 140); // eMMC core reference
  *rate = 196608000;
  return EFI_SUCCESS;
}
static SCMI_CLOCK2_PROTOCOL clock_protocol = { .Enable = enable, .RateGet = rate_get };
static EFI_STATUS EFIAPI execute (A210_SCMI_PROTOCOL *this, UINT8 protocol, UINT8 message,
                                 UINT32 *buffer, UINT32 req, UINT32 *rsp)
{
  assert (req == 12 && *rsp >= 4 && message == 4);
  if (protocol == 0x11)
    assert (buffer[0] == 0 && buffer[1] == 7 && buffer[2] == 0);
  else
  {
    assert (protocol == 0x16 && buffer[2] == 0);
    // Explicit assert uses bit 1; clearing it deasserts. No autonomous pulse.
    assert (buffer[1] == 0 || buffer[1] == 2);
    last_flags = buffer[1];
    resets++;
  }
  buffer[0] = fail_reset ? (UINT32)-4 : 0;
  *rsp = 4;
  return EFI_SUCCESS;
}
static A210_SCMI_PROTOCOL transport = { execute };
static EFI_STATUS EFIAPI attrs (SCMI_PERFORMANCE_PROTOCOL *this, UINT32 id,
                               SCMI_PERFORMANCE_DOMAIN_ATTRIBUTES *attr)
{
  assert (tpl == TPL_NOTIFY && id < 2);
  memset (attr, 0, sizeof (*attr));
  attr->SustainedFreq = 1896000;
  attr->SustainedPerfLevel = 100;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI level (SCMI_PERFORMANCE_PROTOCOL *this, UINT32 id,
                               UINT32 *value)
{
  assert (tpl == TPL_NOTIFY && id < 2);
  *value = id ? 100 : 50;
  return EFI_SUCCESS;
}
static SCMI_PERFORMANCE_PROTOCOL performance = {
  .GetDomainAttributes = attrs, .LevelGet = level
};
static EFI_STATUS EFIAPI locate (EFI_GUID *guid, VOID *registration, VOID **interface)
{
  if (guid == &gArmScmiClock2ProtocolGuid)
    *interface = &clock_protocol;
  else if (guid == &gArmScmiPerformanceProtocolGuid)
    *interface = &performance;
  else if (guid == &gA210ScmiProtocolGuid)
    *interface = &transport;
  else
    return EFI_NOT_FOUND;
  return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES services = {
  .RaiseTPL = raise_tpl, .RestoreTPL = restore_tpl, .LocateProtocol = locate
};
EFI_BOOT_SERVICES *gBS = &services;
INT32 EFIAPI
FdtStringListContains (CONST CHAR8 *list, INT32 length, CONST CHAR8 *string)
{
  return fdt_stringlist_contains (list, length, string);
}
int main (int argc, char **argv)
{
  FILE *file;
  long size;
  void *fdt;
  int emmc, usb;
  UINT64 rate;
  assert (argc == 2 && (file = fopen (argv[1], "rb")));
  assert (!fseek (file, 0, SEEK_END) && (size = ftell (file)) > 0);
  assert (!fseek (file, 0, SEEK_SET) && (fdt = malloc (size)));
  assert (fread (fdt, 1, size, file) == (size_t)size);
  fclose (file);
  emmc = fdt_path_offset (fdt, "/soc/mmc@500000");
  usb = fdt_path_offset (fdt, "/soc/usb@8100000");
  assert (emmc > 0 && usb > 0);
  assert (A210ScmiPowerOn (fdt, emmc) == EFI_SUCCESS);
  assert (A210ScmiClocksEnable (fdt, emmc) == EFI_SUCCESS && clocks == 2);
  assert (A210ScmiClockGet (fdt, emmc, "core", &rate) == EFI_SUCCESS);
  assert (rate == 196608000 && tpl == TPL_APPLICATION);
  assert (A210ScmiReset (fdt, usb, NULL, TRUE) == EFI_SUCCESS && last_flags == 2);
  assert (A210ScmiReset (fdt, usb, NULL, FALSE) == EFI_SUCCESS && last_flags == 0);
  clocks = 0;
  fail_clock = TRUE;
  assert (A210ScmiClocksEnable (fdt, emmc) == EFI_NOT_FOUND && clocks == 2);
  fail_reset = TRUE;
  assert (A210ScmiResetsDeassert (fdt, usb) == EFI_NOT_FOUND);
  assert (A210ScmiCpuRate (fdt, 0, &rate) == EFI_SUCCESS && rate == 948000000);
  assert (A210ScmiCpuRate (fdt, 4, &rate) == EFI_SUCCESS && rate == 1896000000);
  assert (A210ScmiCpuRate (fdt, 8, &rate) == EFI_NOT_FOUND);
  free (fdt);
  puts ("A210 SCMI resources: IDs, reset flags, power arguments and errors passed");
  return 0;
}
