// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardSupportLib.h>

VOID
Th1520Pad (UINTN Base, UINTN Pin, UINT32 Mux, UINT32 Config)
{
  UINTN Shift = 4 * (Pin % 8);
  MmioAndThenOr32 (Base + 0x400 + 4 * (Pin / 8),
                   ~(15U << Shift), Mux << Shift);
  Shift = 16 * (Pin % 2);
  MmioAndThenOr32 (Base + 4 * (Pin / 2),
                   ~(0x3ffU << Shift), Config << Shift);
}

VOID
Th1520GpioOutput (UINTN Base, UINTN Pin, BOOLEAN High)
{
  MmioAndThenOr32 (Base, ~(1U << Pin), High ? 1U << Pin : 0);
  MmioOr32 (Base + 4, 1U << Pin);
  MemoryFence ();
}

STATIC EFI_STATUS
I2cWait (UINTN Base, UINTN Register, UINT32 Mask, UINT32 Value)
{
  UINTN Retry;
  for (Retry = 0; Retry < 10000; Retry++)
  {
    if (MmioRead32 (Base + 0x34) & BIT6)
    {
      MmioRead32 (Base + 0x54);
      return EFI_DEVICE_ERROR;
    }
    if ((MmioRead32 (Base + Register) & Mask) == Value)
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
I2cByte (UINTN Base, UINT8 Register, UINT8 *Value, BOOLEAN Read)
{
  EFI_STATUS Status;
  MmioRead32 (Base + 0x40);
  MmioWrite32 (Base + 0x10, Register);
  MmioWrite32 (Base + 0x10, Read ? BIT8 | BIT9 | BIT10 : BIT9 | *Value);
  if (Read)
  {
    Status = I2cWait (Base, 0x70, BIT3, BIT3);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    *Value = (UINT8)MmioRead32 (Base + 0x10);
  }
  Status = I2cWait (Base, 0x34, BIT9, BIT9);
  MmioRead32 (Base + 0x60);
  return Status;
}

EFI_STATUS
Th1520Pca9557Output (UINTN I2c, UINT8 Address, UINT8 Mask, UINT8 Value)
{
  EFI_STATUS Status;
  UINT8 Output, Config;
  UINTN Retry;
  MmioWrite32 (I2c + 0x6c, 0);
  for (Retry = 0; Retry < 1000 && (MmioRead32 (I2c + 0x9c) & 1); Retry++)
  {
    MicroSecondDelay (10);
  }
  if (Retry == 1000)
  {
    return EFI_TIMEOUT;
  }
  // Master, standard speed, restart, no slave mode.
  MmioWrite32 (I2c, 0x63);
  MmioWrite32 (I2c + 4, Address);
  // Conservative timing for the TH1520 APB clock; setup runs only at boot.
  MmioWrite32 (I2c + 0x14, 2500);
  MmioWrite32 (I2c + 0x18, 2500);
  MmioWrite32 (I2c + 0x7c, 20);
  MmioWrite32 (I2c + 0x30, 0);
  MmioWrite32 (I2c + 0x38, 0);
  MmioWrite32 (I2c + 0x3c, 0);
  MmioRead32 (I2c + 0x40);
  MmioWrite32 (I2c + 0x6c, 1);
  Status = I2cByte (I2c, 1, &Output, TRUE);
  if (!EFI_ERROR (Status))
  {
    Status = I2cByte (I2c, 3, &Config, TRUE);
  }
  if (!EFI_ERROR (Status))
  {
    Output = (Output & ~Mask) | (Value & Mask);
    Status = I2cByte (I2c, 1, &Output, FALSE);
  }
  if (!EFI_ERROR (Status))
  {
    Config &= ~Mask;
    Status = I2cByte (I2c, 3, &Config, FALSE);
  }
  MmioWrite32 (I2c + 0x6c, 0);
  return Status;
}
