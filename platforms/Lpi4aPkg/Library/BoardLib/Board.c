// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/Th1520BoardSupportLib.h>

EFI_STATUS EFIAPI
Th1520BoardUsbRole (BOOLEAN Device)
{
  // AON GPIO4 selects the USB2 hub (high) or USB-C download port (low).
  Th1520GpioOutput (0xfffff41000ULL, 4, !Device);
  // GPIO4 maps to AON pad 13 (CPU_JTG_TDI), whose reset mux is JTAG.
  // Set the GPIO latch/direction first, then connect it to the USB switch.
  Th1520Pad (0xfffff4a000ULL, 13, 3, 4);
  MicroSecondDelay (1000);
  return EFI_SUCCESS;
}
