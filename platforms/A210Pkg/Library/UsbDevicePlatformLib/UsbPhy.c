// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/IoLib.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210ScmiLib.h>
#include <Library/FdtLib.h>
#include <Library/TimerLib.h>
#include <Library/UsbDevicePlatformLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/FdtClient.h>

#define USB_PHY_ANALOG (A210_USB_PHY_BASE + 0x000)
#define USB_PHY_CONFIG (A210_USB_PHY_BASE + 0x004)
#define USB_PHY_DM_PULLDOWN BIT1
#define USB_TX_LEVEL_MASK (15U << 17)
#define USB_TX_LEVEL (11U << 17)

EFI_STATUS
UsbDevicePlatformInit (UINTN *Base)
{
  CONST VOID *Fdt = A210GetFdt ();
  INT32 Node, Phy;
  UINTN Address, Size;
  EFI_STATUS Status;
  if (!Base || !Fdt)
    return EFI_INVALID_PARAMETER;
  Node = FdtNodeOffsetByCompatible (Fdt, -1, "zhihe,a210-usb");
  if (!A210NodeEnabled (Fdt, Node)
      || EFI_ERROR (A210Reg (Fdt, Node, 0, &Address, &Size))
      || Address != A210_USB_DEVICE_BASE || Size < 0x40000)
    return EFI_UNSUPPORTED;
  Phy = A210Reference (Fdt, Node, "phys");
  if (!A210NodeEnabled (Fdt, Phy)
      || !A210PropertyIs (Fdt, Phy, "compatible", "zhihe,a210-usb2-phy"))
    return EFI_UNSUPPORTED;
  Status = A210ScmiPowerOn (Fdt, Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Fdt, Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Fdt, Phy);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Fdt, Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiResetsDeassert (Fdt, Phy);
  if (EFI_ERROR (Status))
    return Status;
  MmioOr32 (USB_PHY_CONFIG, USB_PHY_DM_PULLDOWN);
  MmioAndThenOr32 (USB_PHY_ANALOG, ~USB_TX_LEVEL_MASK, USB_TX_LEVEL);
  MicroSecondDelay (1000);
  *Base = A210_USB_DEVICE_BASE;
  return EFI_SUCCESS;
}
