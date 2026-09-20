// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Fastboot.h"
#include <Library/PrintLib.h>
#define SPI 0xffea000000ULL
#define GPIO 0xffe7f34000ULL
#define PAD 0xffec007000ULL
#define FLASH_SIZE SIZE_8MB
STATIC UINT32 FrameSize;
STATIC VOID
ChipSelect (BOOLEAN High)
{
  if (High)
  {
    MmioOr32 (GPIO, BIT3);
  }
  else
  {
    MmioAnd32 (GPIO, ~BIT3);
  }
}
STATIC EFI_STATUS
Exchange (CONST UINT8 *Tx, UINT8 *Rx, UINTN Size)
{
  UINTN I, N;
  UINT32 Value;
  MmioWrite32 (SPI + 8, 0);
  MmioWrite32 (SPI, FrameSize);
  MmioWrite32 (SPI + 0x10, 1);
  MmioWrite32 (SPI + 8, 1);
  ChipSelect (FALSE);
  for (I = 0; I < Size; I++)
  {
    MmioWrite32 (SPI + 0x60, Tx ? Tx[I] : 0xff);
    for (N = 0; N < 10000; N++)
    {
      if (MmioRead32 (SPI + 0x28) & BIT3)
      {
        break;
      }
      MicroSecondDelay (1);
    }
    if (N == 10000)
    {
      ChipSelect (TRUE);
      MmioWrite32 (SPI + 8, 0);
      return EFI_TIMEOUT;
    }
    Value = MmioRead32 (SPI + 0x60);
    if (Rx)
    {
      Rx[I] = (UINT8)Value;
    }
  }
  for (N = 0; N < 10000; N++)
  {
    if (!(MmioRead32 (SPI + 0x28) & BIT0))
    {
      break;
    }
    MicroSecondDelay (1);
  }
  ChipSelect (TRUE);
  MmioWrite32 (SPI + 8, 0);
  return N == 10000 ? EFI_TIMEOUT : EFI_SUCCESS;
}
STATIC EFI_STATUS
Ready (VOID)
{
  UINT8 Tx[2] = {5, 0}, Rx[2];
  UINTN N;
  EFI_STATUS Status;
  for (N = 0; N < 5000; N++)
  {
    Status = Exchange (Tx, Rx, 2);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    if (!(Rx[1] & 1))
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1000);
  }
  return EFI_TIMEOUT;
}
STATIC EFI_STATUS
WriteEnable (VOID)
{
  UINT8 Cmd = 6, Tx[2] = {5, 0}, Rx[2];
  EFI_STATUS Status;
  Status = Exchange (&Cmd, NULL, 1);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Exchange (Tx, Rx, 2);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  return (Rx[1] & 2) ? EFI_SUCCESS : EFI_WRITE_PROTECTED;
}
STATIC VOID
AddressCommand (UINT8 *Cmd, UINT8 Op, UINTN Address)
{
  Cmd[0] = Op;
  Cmd[1] = Address >> 16;
  Cmd[2] = Address >> 8;
  Cmd[3] = Address;
}
EFI_STATUS
SpiFlash (CONST VOID *Buffer, UINTN Length)
{
  UINT8 Cmd[260], Readback[260], Id[4], Probe[4] = {0x9f, 0, 0, 0};
  UINTN Offset, Count, Pin, Shift, Reg;
  EFI_STATUS Status;
  CHAR8 Progress[64];
  if (Length == 0 || Length > FLASH_SIZE)
  {
    return EFI_BAD_BUFFER_SIZE;
  }
  MmioOr32 (0xffef010204ULL, BIT17 | BIT6);
  ChipSelect (TRUE);
  MmioOr32 (GPIO + 4, BIT3);
  for (Pin = 2; Pin <= 8; Pin++)
  {
    if (Pin == 4)
    {
      continue; // PWM2 cooling fan shares QSPI CS1.
    }
    Reg = PAD + 0x400 + 4 * (Pin / 8);
    Shift = 4 * (Pin % 8);
    MmioAndThenOr32 (Reg, ~(15U << Shift), Pin == 3 ? 3U << Shift : 0);
    Reg = PAD + 4 * (Pin / 2);
    Shift = 16 * (Pin % 2);
    MmioAndThenOr32 (Reg, ~(0x3ffU << Shift), (Pin == 3 ? 4U : 0x204U) << Shift);
  }
  MmioWrite32 (SPI + 8, 0);
  MmioWrite32 (SPI, 0xffffffff);
  FrameSize = (MmioRead32 (SPI) & 15) ? 7 : 7U << 16;
  // VIDEO_PLL is at most 3 GHz; /400 stays below the board's 10 MHz limit.
  MmioWrite32 (SPI + 0x14, 400);
  MmioWrite32 (SPI + 0x2c, 0);
  Status = Exchange (Probe, Id, sizeof (Id));
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (Id[1] != 0xef || (Id[2] != 0x60 && Id[2] != 0x80) || Id[3] != 0x17)
  {
    return EFI_UNSUPPORTED;
  }
  Status = Ready ();
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  for (Offset = 0; Offset < ALIGN_VALUE (Length, SIZE_4KB); Offset += SIZE_4KB)
  {
    if ((Offset & 0xffff) == 0)
    {
      AsciiSPrint (Progress, sizeof (Progress), "INFOWriting SPI 0x%08x", (UINT32)Offset);
      Status = UsbSend (Progress, AsciiStrLen (Progress));
      if (EFI_ERROR (Status))
      {
        return Status;
      }
    }
    Status = WriteEnable ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    AddressCommand (Cmd, 0x20, Offset);
    Status = Exchange (Cmd, NULL, 4);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Status = Ready ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    for (Count = Offset; Count < MIN (Offset + SIZE_4KB, Length); Count += 256)
    {
      UINTN Bytes = MIN (256, Length - Count);
      Status = WriteEnable ();
      if (EFI_ERROR (Status))
      {
        return Status;
      }
      AddressCommand (Cmd, 2, Count);
      CopyMem (Cmd + 4, (CONST UINT8 *)Buffer + Count, Bytes);
      Status = Exchange (Cmd, NULL, 4 + Bytes);
      if (EFI_ERROR (Status))
      {
        return Status;
      }
      Status = Ready ();
      if (EFI_ERROR (Status))
      {
        return Status;
      }
      AddressCommand (Cmd, 3, Count);
      SetMem (Cmd + 4, Bytes, 0xff);
      Status = Exchange (Cmd, Readback, 4 + Bytes);
      if (EFI_ERROR (Status))
      {
        return Status;
      }
      if (CompareMem (Readback + 4, (CONST UINT8 *)Buffer + Count, Bytes))
      {
        return EFI_CRC_ERROR;
      }
    }
  }
  return EFI_SUCCESS;
}
