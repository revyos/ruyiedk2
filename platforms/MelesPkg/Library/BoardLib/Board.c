// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardLib.h>
#include <Library/Th1520BoardSupportLib.h>

EFI_STATUS EFIAPI
Th1520BoardUsbRole (BOOLEAN Device)
{
  MmioOr32 (0xffef010204ULL, BIT22 | BIT6);
  Th1520GpioOutput (0xffe7f34000ULL, 30, Device);
  Th1520Pad (0xffec007000ULL, 30, 0, 2);
  MicroSecondDelay (1000);
  return EFI_SUCCESS;
}
