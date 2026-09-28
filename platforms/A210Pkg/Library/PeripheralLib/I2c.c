// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/A210PeripheralLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define IC_CON 0x00
#define IC_TAR 0x04
#define IC_DATA_CMD 0x10
#define IC_SS_HCNT 0x14
#define IC_SS_LCNT 0x18
#define IC_INTR_MASK 0x30
#define IC_RAW_INTR 0x34
#define IC_CLR_INTR 0x40
#define IC_ENABLE 0x6c
#define IC_STATUS 0x70
#define IC_SDA_HOLD 0x7c
#define IC_ENABLE_STATUS 0x9c
#define IC_TX_ABORT BIT6
#define IC_STOP_DETECTED BIT9
#define IC_TX_SPACE BIT1
#define IC_RX_DATA BIT3
#define IC_READ BIT8
#define IC_STOP BIT9
#define IC_RESTART BIT10
#define IC_MASTER_STANDARD (BIT0 | BIT1 | BIT5 | BIT6)

#define I2C_TIMEOUT_NS 2000000ULL

STATIC BOOLEAN
Expired (UINT64 Start)
{
  return GetTimeInNanoSecond (GetPerformanceCounter () - Start)
         >= I2C_TIMEOUT_NS;
}

STATIC EFI_STATUS
Wait (UINTN Offset, UINT32 Mask, UINT32 Value, UINT64 Start)
{
  do
  {
    if ((MmioRead32 (A210_AON_I2C1_BASE + Offset) & Mask) == Value)
      return EFI_SUCCESS;
    MicroSecondDelay (1);
  } while (!Expired (Start));
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
I2cByte (UINT8 Address, UINT8 Register, UINT8 *Data, BOOLEAN Read)
{
  STATIC CONST UINT32 Sources[] = {110000000, 24000000, 26000000, 32768};
  UINT32 Clock, Hz, Count;
  UINTN Pin;
  UINT64 Start = GetPerformanceCounter ();
  EFI_STATUS Status;
  Clock = MmioRead32 (A210_AON_CLOCK_BASE);
  Hz = Sources[(Clock >> 4) & 3] / ((Clock & 7) + 1);
  if (Hz < 1000000)
    return EFI_UNSUPPORTED;
  MmioOr32 (A210_AON_CLOCK_BASE + 4, BIT20 | BIT21);
  MmioOr32 (A210_AON_RESET_BASE + 0x10, BIT1);
  for (Pin = 15; Pin <= 16; Pin++)
  {
    UINTN Shift = Pin % 2 * 16;
    MmioAnd32 (A210_AON_PAD_BASE + 0x400 + Pin / 8 * 4,
               ~(0xfU << (Pin % 8 * 4)));
    MmioAndThenOr32 (A210_AON_PAD_BASE + Pin / 2 * 4,
                     ~(0x3ffU << Shift),
                     (BIT9 | BIT8 | BIT7 | 15U) << Shift);
  }
  Status = Wait (IC_STATUS, BIT0, 0, Start);
  if (EFI_ERROR (Status))
    return Status;
  MmioWrite32 (A210_AON_I2C1_BASE + IC_ENABLE, 0);
  Status = Wait (IC_ENABLE_STATUS, BIT0, 0, Start);
  if (EFI_ERROR (Status))
    return Status;
  Count = (Hz + 199999) / 200000;
  MmioWrite32 (A210_AON_I2C1_BASE + IC_CON, IC_MASTER_STANDARD);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_SS_HCNT, Count);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_SS_LCNT, Count);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_SDA_HOLD, (Hz + 2999999) / 3000000);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_INTR_MASK, 0);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_TAR, Address);
  MmioRead32 (A210_AON_I2C1_BASE + IC_CLR_INTR);
  MmioWrite32 (A210_AON_I2C1_BASE + IC_ENABLE, 1);
  Status = Wait (IC_STATUS, IC_TX_SPACE, IC_TX_SPACE, Start);
  if (EFI_ERROR (Status))
    goto Done;
  MmioWrite32 (A210_AON_I2C1_BASE + IC_DATA_CMD, Register);
  Status = Wait (IC_STATUS, IC_TX_SPACE, IC_TX_SPACE, Start);
  if (EFI_ERROR (Status))
    goto Done;
  MmioWrite32 (A210_AON_I2C1_BASE + IC_DATA_CMD,
                IC_STOP | (Read ? IC_READ | IC_RESTART : *Data));
  Status = EFI_TIMEOUT;
  while (!Expired (Start))
  {
    UINT32 Interrupt = MmioRead32 (A210_AON_I2C1_BASE + IC_RAW_INTR);
    if (Interrupt & IC_TX_ABORT)
    {
      Status = EFI_DEVICE_ERROR;
      break;
    }
    if (Interrupt & IC_STOP_DETECTED)
    {
      Status = EFI_SUCCESS;
      break;
    }
    // Bounded polling on the AP-owned peripheral bus only.
    MicroSecondDelay (1);
  }
  if (!EFI_ERROR (Status) && Read)
  {
    if (!(MmioRead32 (A210_AON_I2C1_BASE + IC_STATUS) & IC_RX_DATA))
      Status = EFI_DEVICE_ERROR;
    else
      *Data = MmioRead32 (A210_AON_I2C1_BASE + IC_DATA_CMD);
  }
Done:
  MmioWrite32 (A210_AON_I2C1_BASE + IC_ENABLE, 0);
  MmioRead32 (A210_AON_I2C1_BASE + IC_CLR_INTR);
  return Status;
}

EFI_STATUS
A210I2cDevice (CONST VOID *Fdt, INT32 Node, A210_I2C_DEVICE *Device)
{
  CONST UINT32 *Reg;
  UINTN Base, Size;
  INT32 Length, Bus = FdtParentOffset (Fdt, Node);

  if (!Device || !A210NodeEnabled (Fdt, Node)
      || !A210NodeEnabled (Fdt, Bus))
    return EFI_INVALID_PARAMETER;
  Reg = FdtGetProp (Fdt, Node, "reg", &Length);
  if (!Reg || Length != 4 || A210Cell (Reg) > 0x7f
      || EFI_ERROR (A210Reg (Fdt, Bus, 0, &Base, &Size))
      || Base != A210_AON_I2C1_BASE || Size < 0x100)
    return EFI_UNSUPPORTED;
  Device->Address = A210Cell (Reg);
  return EFI_SUCCESS;
}

EFI_STATUS
A210I2cByte (CONST A210_I2C_DEVICE *Device, UINT8 Register,
             UINT8 *Data, BOOLEAN Read)
{
  EFI_STATUS Status;
  EFI_TPL Tpl;

  if (!Device || !Data || Device->Address > 0x7f)
    return EFI_INVALID_PARAMETER;
  // Resolve the DT once; never scan it from the Type-C polling path.
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = I2cByte (Device->Address, Register, Data, Read);
  gBS->RestoreTPL (Tpl);
  return Status;
}
