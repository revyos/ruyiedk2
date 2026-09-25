// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/Th1520SpiLib.h>
#include "RuntimeIo.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
STATIC UINTN SPI = 0xffea000000ULL;
STATIC UINTN GPIO = 0xffe7f34000ULL;
STATIC UINTN PAD = 0xffec007000ULL;
STATIC UINTN AP = 0xffef010000ULL;
#define FLASH_SIZE SIZE_8MB
STATIC UINT32 FrameSize, FifoDepth;
STATIC BOOLEAN NativeCs;
STATIC VOID
ChipSelect (BOOLEAN High)
{
  if (NativeCs)
  {
    Th1520SpiMmioWrite32 (SPI + 0x10, High ? 0 : 1);
    return;
  }
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
  UINTN Sent = 0, Received = 0, Count, Before, N;
  UINT64 Progress;
  UINT8 Value;
  if (NativeCs && Size > FifoDepth)
    return EFI_BAD_BUFFER_SIZE;
  Th1520SpiMmioWrite32 (SPI + 8, 0);
  Th1520SpiMmioWrite32 (SPI, FrameSize);
  Th1520SpiMmioWrite32 (SPI + 0x10, NativeCs ? 0 : 1);
  Th1520SpiMmioWrite32 (SPI + 8, 1);
  if (NativeCs)
  {
    // DW SSI drops CS when TX runs empty. Queue the whole transaction
    // before selecting the chip; RX also fits without a refill deadline.
    while (Sent < Size)
    {
      Th1520SpiMmioWrite32 (SPI + 0x60, Tx ? Tx[Sent] : 0xff);
      Sent++;
    }
  }
  ChipSelect (FALSE);
  Progress = GetPerformanceCounter ();
  while (Received < Size)
  {
    Before = Received;
    // Include frames still in TX when reserving RX FIFO capacity.
    Count = MIN (Size - Sent, FifoDepth - (Sent - Received));
    while (Count--)
    {
      Th1520SpiMmioWrite32 (SPI + 0x60, Tx ? Tx[Sent] : 0xff);
      Sent++;
    }
    Count = MIN (Th1520SpiMmioRead32 (SPI + 0x24), Sent - Received);
    while (Count--)
    {
      Value = (UINT8)Th1520SpiMmioRead32 (SPI + 0x60);
      if (Rx)
      {
        Rx[Received] = Value;
      }
      Received++;
    }
    if (Received != Before)
    {
      Progress = GetPerformanceCounter ();
    }
    else if (GetTimeInNanoSecond (GetPerformanceCounter () - Progress) >
             100000000)
    {
      ChipSelect (TRUE);
      Th1520SpiMmioWrite32 (SPI + 8, 0);
      return EFI_TIMEOUT;
    }
  }
  for (N = 0; N < 10000; N++)
  {
    if (!(Th1520SpiMmioRead32 (SPI + 0x28) & BIT0))
    {
      break;
    }
    MicroSecondDelay (1);
  }
  ChipSelect (TRUE);
  Th1520SpiMmioWrite32 (SPI + 8, 0);
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
STATIC EFI_STATUS
Initialize (BOOLEAN Pins)
{
  UINT8 Id[4], Probe[4] = {0x9f, 0, 0, 0};
  UINTN Pin, Shift, Reg;
  EFI_STATUS Status;
  UINT32 Pll, Cfg, Div, Divider;
  UINT64 Rate;
  if (Pins)
  {
    MmioOr32 (AP + 0x204, BIT17 | BIT6 | BIT22);
    if (!NativeCs)
    {
      ChipSelect (TRUE);
      MmioOr32 (GPIO + 4, BIT3);
    }
    for (Pin = 2; Pin <= 8; Pin++)
    {
      if (Pin == 4)
      {
        continue; // PWM2 cooling fan shares QSPI CS1.
      }
      Reg = PAD + 0x400 + 4 * (Pin / 8);
      Shift = 4 * (Pin % 8);
      MmioAndThenOr32 (Reg, ~(15U << Shift),
                       Pin == 3 && !NativeCs ? 3U << Shift : 0);
      Reg = PAD + 4 * (Pin / 2);
      Shift = 16 * (Pin % 2);
      MmioAndThenOr32 (Reg, ~(0x3ffU << Shift),
                       (Pin == 3 ? 4U : 0x204U) << Shift);
    }
  }
  else if (!(Th1520SpiMmioRead32 (AP + 0x204) & BIT17))
  {
    // No runtime writes to the shared clock/pinctrl/GPIO registers.
    return EFI_NOT_READY;
  }
  Th1520SpiMmioWrite32 (SPI + 8, 0);
  Th1520SpiMmioWrite32 (SPI, 0xffffffff);
  FrameSize = (Th1520SpiMmioRead32 (SPI) & 15) ? 7 : 7U << 16;
  // Read the QSPI parent rate without changing the shared VIDEO_PLL.
  Pll = Th1520SpiMmioRead32 (AP + 0x30);
  Cfg = Th1520SpiMmioRead32 (AP + 0x34);
  Div = (Pll & 63) * ((Pll >> 20) & 7) * ((Pll >> 24) & 7);
  if (!Div)
  {
    return EFI_DEVICE_ERROR;
  }
  Rate = LShiftU64 ((Pll >> 8) & 4095, 24);
  if (!(Cfg & BIT24))
  {
    Rate += Cfg & 0xffffff;
  }
  Rate = (Cfg & BIT30) ? 24000000 :
      DivU64x32 (RShiftU64 (MultU64x32 (Rate, 24000000), 24), Div);
  Divider = (UINT32)DivU64x32 (Rate + 9999999, 10000000);
  Divider = (MAX (Divider, 2) + 1) & ~1U;
  if (Divider > 65534)
  {
    return EFI_UNSUPPORTED;
  }
  Th1520SpiMmioWrite32 (SPI + 0x14, Divider);
  for (FifoDepth = 1; FifoDepth < 256; FifoDepth++)
  {
    Th1520SpiMmioWrite32 (SPI + 0x18, FifoDepth);
    if (Th1520SpiMmioRead32 (SPI + 0x18) != FifoDepth)
    {
      break;
    }
  }
  Th1520SpiMmioWrite32 (SPI + 0x18, 0);
  Th1520SpiMmioWrite32 (SPI + 0x2c, 0);
  if (FifoDepth <= 4)
    return EFI_UNSUPPORTED;
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
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520SpiInit (VOID)
{
  NativeCs = FALSE;
  return Initialize (TRUE);
}

EFI_STATUS
Th1520SpiInitNative (VOID)
{
  NativeCs = TRUE;
  return Initialize (TRUE);
}

EFI_STATUS
Th1520SpiRuntimeInit (VOID)
{
  NativeCs = TRUE;
  return Initialize (FALSE);
}

EFI_STATUS
Th1520SpiConvertPointers (EFI_CONVERT_POINTER Convert)
{
  EFI_STATUS Status = Convert (0, (VOID **)&SPI);
  if (EFI_ERROR (Status))
    return Status;
  Status = Convert (0, (VOID **)&AP);
  return EFI_ERROR (Status) ? Status :
      Th1520SpiMapRuntimeIo (Convert, SPI, AP);
}

EFI_STATUS
Th1520SpiRead (UINTN Offset, UINTN Length, VOID *Buffer)
{
  UINT8 Cmd[260], Rx[260];
  if (!Buffer || Offset > FLASH_SIZE || Length > FLASH_SIZE - Offset)
    return EFI_INVALID_PARAMETER;
  while (Length)
  {
    UINTN Count = MIN (Length, 256);
    if (NativeCs)
      Count = MIN (Count, FifoDepth - 4);
    AddressCommand (Cmd, 3, Offset);
    SetMem (Cmd + 4, Count, 0xff);
    EFI_STATUS Status = Exchange (Cmd, Rx, Count + 4);
    if (EFI_ERROR (Status))
      return Status;
    CopyMem (Buffer, Rx + 4, Count);
    Buffer = (UINT8 *)Buffer + Count;
    Offset += Count;
    Length -= Count;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520SpiErase (UINTN Offset, UINTN Length)
{
  UINT8 Cmd[4];
  if (Offset % SIZE_4KB || Length % SIZE_4KB ||
      Offset > FLASH_SIZE || Length > FLASH_SIZE - Offset)
    return EFI_INVALID_PARAMETER;
  while (Length)
  {
    EFI_STATUS Status = WriteEnable ();
    if (!EFI_ERROR (Status))
    {
      AddressCommand (Cmd, 0x20, Offset);
      Status = Exchange (Cmd, NULL, 4);
    }
    if (!EFI_ERROR (Status))
      Status = Ready ();
    if (EFI_ERROR (Status))
      return Status;
    Offset += SIZE_4KB;
    Length -= SIZE_4KB;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520SpiProgram (UINTN Offset, UINTN Length, CONST VOID *Buffer)
{
  UINT8 Cmd[260], Verify[256];
  if (!Buffer || Offset > FLASH_SIZE || Length > FLASH_SIZE - Offset)
    return EFI_INVALID_PARAMETER;
  while (Length)
  {
    UINTN Count = MIN (Length, 256 - Offset % 256);
    if (NativeCs)
      Count = MIN (Count, FifoDepth - 4);
    EFI_STATUS Status = WriteEnable ();
    if (!EFI_ERROR (Status))
    {
      AddressCommand (Cmd, 2, Offset);
      CopyMem (Cmd + 4, Buffer, Count);
      Status = Exchange (Cmd, NULL, Count + 4);
    }
    if (!EFI_ERROR (Status))
      Status = Ready ();
    if (!EFI_ERROR (Status))
      Status = Th1520SpiRead (Offset, Count, Verify);
    if (EFI_ERROR (Status))
      return Status;
    if (CompareMem (Buffer, Verify, Count))
      return EFI_CRC_ERROR;
    Buffer = (CONST UINT8 *)Buffer + Count;
    Offset += Count;
    Length -= Count;
  }
  return EFI_SUCCESS;
}
