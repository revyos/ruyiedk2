// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/Th1520BoardSupportLib.h>

VOID EFIAPI
Th1520BoardEarlyInit (VOID)
{
  Th1520Fan (4, 1, 2);

  // USB-A VBUS is separate from the GL3523's fixed 3.3 V / 1.2 V rails.
  // Establish its active level before connecting the pad, then leave it
  // powered through host/device switches and the handoff to Linux.
  MmioOr32 (0xffef010204ULL, BIT6 | BIT24);
  Th1520GpioOutput (0xffec005000ULL, 27, TRUE);
  Th1520Pad (0xffe7f3c000ULL, 27, 0, 4); // 7 mA, matching the board DT.
  MicroSecondDelay (1000);
}

EFI_STATUS EFIAPI
Th1520BoardUsbRole (BOOLEAN Device)
{
  MmioOr32 (0xffef010204ULL, BIT22 | BIT8);
  Th1520GpioOutput (0xffe7f34000ULL, 30, Device);
  Th1520Pad (0xffec007000ULL, 30, 0, 2);
  MicroSecondDelay (1000);
  return EFI_SUCCESS;
}
