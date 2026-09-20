// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/Th1520BoardSupportLib.h>

VOID EFIAPI
Th1520BoardEarlyInit (VOID)
{
  Th1520Fan (35, 1, 1); // GPIO3_3, PWM1.

  // Establish USB-A VBUS before connecting GPIO1_22 to the pad. Keep it
  // powered across USB role changes and the Linux regulator handoff.
  MmioOr32 (0xffef010204ULL, BIT7 | BIT24);
  Th1520GpioOutput (0xffec006000ULL, 22, TRUE);
  Th1520Pad (0xffe7f3c000ULL, 54, 3, 4);
  MicroSecondDelay (1000);
}

EFI_STATUS EFIAPI
Th1520BoardUsbRole (BOOLEAN Device)
{
  EFI_STATUS Status;
  // AON GPIO4 selects the USB2 hub (high) or USB-C download port (low).
  Th1520GpioOutput (0xfffff41000ULL, 4, !Device);
  // GPIO4 maps to AON pad 13 (CPU_JTG_TDI), whose reset mux is JTAG.
  // Set the GPIO latch/direction first, then connect it to the USB switch.
  Th1520Pad (0xfffff4a000ULL, 13, 3, 4);
  MicroSecondDelay (1000);
  if (Device)
  {
    return EFI_SUCCESS;
  }
  MmioOr32 (0xffef010204ULL, BIT2 | BIT22);
  Th1520Pad (0xffec007000ULL, 11, 0, BIT9 | BIT7 | BIT6 | 4);
  Th1520Pad (0xffec007000ULL, 12, 0, BIT9 | BIT7 | BIT6 | 4);
  Status = Th1520Pca9557Output (0xffec014000ULL, 0x18, BIT3, BIT3);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  MicroSecondDelay (10000);
  return EFI_SUCCESS;
}
