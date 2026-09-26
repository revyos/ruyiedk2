// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UsbDevicePlatformLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/FdtClient.h>

#define USB_BLOCK_CLOCK (A210_USB_CPR_BASE + 0x010)
#define USB_WRAP_CLOCK (A210_USB_CPR_BASE + 0x014)
#define USB_BLOCK_RESET (A210_USB_CPR_BASE + 0x110)
#define USB_WRAP_RESET (A210_USB_CPR_BASE + 0x114)
#define USB_PHY_RESET (A210_USB_CPR_BASE + 0x118)
#define USB_BLOCK_GATES                                                   \
  (BIT0 | BIT4 | BIT8 | BIT16 | BIT20 | BIT24 | BIT28)
#define USB_WRAP0_GATES (BIT0 | BIT16 | BIT20)
#define USB_WRAP0_RESETS (BIT0 | BIT4 | BIT16 | BIT20)
#define USB_PHY0_RESET_N BIT0
#define USB_PHY_ANALOG (A210_USB_PHY_BASE + 0x000)
#define USB_PHY_CONFIG (A210_USB_PHY_BASE + 0x004)
#define USB_PHY_DM_PULLDOWN BIT1
#define USB_TX_LEVEL_MASK (15U << 17)
#define USB_TX_LEVEL (11U << 17)

EFI_STATUS
UsbDevicePlatformInit (UINTN *Base)
{
  if (Base == NULL)
    return EFI_INVALID_PARAMETER;
  MmioOr32 (USB_BLOCK_CLOCK, USB_BLOCK_GATES);
  MmioOr32 (USB_WRAP_CLOCK, USB_WRAP0_GATES);
  MmioOr32 (USB_BLOCK_RESET, USB_BLOCK_GATES);
  MmioOr32 (USB_WRAP_RESET, USB_WRAP0_RESETS);
  MmioOr32 (USB_PHY_RESET, USB_PHY0_RESET_N);
  MmioOr32 (USB_PHY_CONFIG, USB_PHY_DM_PULLDOWN);
  MmioAndThenOr32 (USB_PHY_ANALOG, ~USB_TX_LEVEL_MASK, USB_TX_LEVEL);
  MicroSecondDelay (1000);
  *Base = A210_USB_DEVICE_BASE;
  return EFI_SUCCESS;
}
