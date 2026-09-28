// SPDX-License-Identifier: GPL-2.0-only
// A210 USB integration, following the Linux A210 PHY drivers.
#include <Uefi.h>
#include <A210.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210UsbHostLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define USB_CLOCK_COMMON (A210_USB_CPR_BASE + 0x00)
#define USB_RESET_COMMON (A210_USB_CPR_BASE + 0x100)
#define USB_BUS_GATES (BIT0 | BIT4 | BIT8 | BIT12 | BIT16)
#define USB_BUS_RESETS (BIT0 | BIT4 | BIT8 | BIT12)
#define USB31_CLOCK (A210_USB_CPR_BASE + 0x08)
#define USB31_RESET (A210_USB_CPR_BASE + 0x108)
#define USB31_GATES (BIT0 | BIT4 | BIT8 | BIT16 | BIT20)
#define USB31_APB_RESET BIT0
#define USB31_CORE_RESET BIT4
#define USB31_PHY_RESET BIT8
#define USB31_AFENCE_RESET BIT12
#define USB_C10_CLOCK (A210_USB_CPR_BASE + 0x0c)
#define USB_C10_RESET (A210_USB_CPR_BASE + 0x10c)
#define C10_PHY_RESET BIT0
#define C10_APB_RESET BIT4
#define USB20_CLOCK (A210_USB_CPR_BASE + 0x10)
#define USB20_WRAP_CLOCK (A210_USB_CPR_BASE + 0x14)
#define USB20_RESET (A210_USB_CPR_BASE + 0x110)
#define USB20_WRAP_RESET (A210_USB_CPR_BASE + 0x114)
#define USB20_PHY_RESET (A210_USB_CPR_BASE + 0x118)
#define USB20_GATES (BIT0 | BIT4 | BIT8 | BIT16 | BIT20 | BIT24 | BIT28)
#define USB20_WRAP1_GATES (BIT4 | BIT24 | BIT28)
#define USB20_WRAP1_RESETS (BIT8 | BIT12 | BIT24 | BIT28)
#define USB20_WRAP1_CORE_RESET BIT12
#define USB20_PHY1_RESET BIT4
#define PHY_ANALOG 0x00
#define PHY_CONFIG 0x04
#define PHY_TX_LEVEL_MASK (15U << 17)
#define PHY_TX_LEVEL (11U << 17)
#define PHY1_PULLDOWNS (BIT2 | BIT3)
#define USB31_OTG_CONTROL 0x20
#define USB31_VBUS_COMPARE BIT12
#define USB31_OTG_DISABLE BIT4
#define TCA_CLOCK_RESET 0x00
#define TCA_RESET_RELEASE BIT9
#define TCA_IRQ_STATUS 0x08
#define TCA_GLOBAL 0x10
#define TCA_MODE_MASK 3U
#define TCA_SYNC_MODE 1U
#define TCA_TCPC 0x14
#define TCA_TCPC_VALID BIT4
#define TCA_TCPC_USB BIT0
#define TCA_SYSTEM 0x18
#define TCA_SYSTEM_DISABLE BIT3
#define TCA_SYSTEM_FLIP BIT2
#define TCA_SYSTEM_CONNECTION 3U
#define DWC3_GCTL 0xc110
#define DWC3_GSNPSID 0xc120
#define DWC3_USB2PHYCFG 0xc200
#define DWC3_USB3PIPECTL 0xc2c0
#define DWC3_CORE_RESET BIT11
#define DWC3_HOST (1U << 12)
#define DWC3_PORT_CAP (3U << 12)

STATIC EFI_STATUS
PhyRegisters (CONST VOID *Fdt, INT32 Node, UINTN Index,
              CONST CHAR8 *Compatible, UINTN *Base)
{
  CONST UINT32 *Phys;
  UINTN Size;
  INT32 Length, Phy;
  Phys = FdtGetProp (Fdt, Node, "phys", &Length);
  if (!Phys || Length < (Index + 1) * 4)
    return EFI_UNSUPPORTED;
  Phy = FdtNodeOffsetByPhandle (Fdt, A210Cell (Phys + Index));
  if (!A210NodeEnabled (Fdt, Phy)
      || !A210PropertyIs (Fdt, Phy, "compatible", Compatible))
    return EFI_UNSUPPORTED;
  // A fixed host uses the normal routing; a TCPC must own DRD switching.
  if (FdtGetProp (Fdt, Phy, "orientation-switch", NULL))
    return EFI_UNSUPPORTED;
  return A210Reg (Fdt, Phy, 0, Base, &Size);
}

EFI_STATUS
A210UsbHostInit (BOOLEAN Usb3, UINTN *Base)
{
  VOID *Fdt = A210GetFdt ();
  INT32 Node = -1;
  UINTN Size, Phy, Tca;
  EFI_STATUS Status;
  CONST CHAR8 *Compatible = Usb3 ? "zhihe,a210-dwc3"
                                 : "zhihe,a210-usb";
  if (!Fdt || !Base)
    return EFI_INVALID_PARAMETER;
  while ((Node = FdtNodeOffsetByCompatible (Fdt, Node, Compatible)) >= 0)
    if (A210NodeEnabled (Fdt, Node)
        && A210PropertyIs (Fdt, Node, "dr_mode", "host"))
      break;
  if (Node < 0)
    return EFI_NOT_FOUND;
  Status = A210Reg (Fdt, Node, 0, Base, &Size);
  if (EFI_ERROR (Status) || Size < 0x10000
      || *Base != (Usb3 ? A210_USB3_BASE : A210_USB2_HOST_BASE))
    return EFI_UNSUPPORTED;
  Status = PhyRegisters (Fdt, Node, 0,
      Usb3 ? "zhihe,a210-usb31-usb2-phy" : "zhihe,a210-usb2-phy1", &Phy);
  if (EFI_ERROR (Status))
    return Status;
  if (Phy != (Usb3 ? (A210_USB31_SYSREG_BASE + 0x80)
                  : A210_USB2_PHY1_BASE))
    return EFI_UNSUPPORTED;
  if (Usb3)
  {
    Status = PhyRegisters (Fdt, Node, 1, "zhihe,a210-c10-phy", &Tca);
    if (EFI_ERROR (Status) || Tca != A210_USB_C10_BASE)
      return EFI_UNSUPPORTED;
  }
  Status = A210GpioHogs (Fdt);
  if (EFI_ERROR (Status))
    return Status;
  Status = A210Supply (Fdt, Node, "vbus-supply");
  // Explicit fixed-host DTs may provide VBUS through a GPIO hog.
  if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
    return Status;
  MmioOr32 (USB_CLOCK_COMMON, USB_BUS_GATES);
  MmioOr32 (USB_RESET_COMMON, USB_BUS_RESETS);
  if (!Usb3)
  {
    MmioOr32 (USB20_CLOCK, USB20_GATES);
    MmioOr32 (USB20_WRAP_CLOCK, USB20_WRAP1_GATES);
    MmioOr32 (USB20_RESET, USB20_GATES);
    // Linux releases the wrapper before initializing the PHY.
    MmioOr32 (USB20_WRAP_RESET, USB20_WRAP1_RESETS);
    MmioAnd32 (USB20_WRAP_RESET, ~USB20_WRAP1_CORE_RESET);
    MicroSecondDelay (10);
    MmioOr32 (USB20_WRAP_RESET, USB20_WRAP1_CORE_RESET);
    MmioAnd32 (USB20_PHY_RESET, ~USB20_PHY1_RESET);
    MicroSecondDelay (100);
    MmioAndThenOr32 (Phy + PHY_ANALOG, ~PHY_TX_LEVEL_MASK, PHY_TX_LEVEL);
    MmioOr32 (Phy + PHY_CONFIG, PHY1_PULLDOWNS);
    MmioOr32 (USB20_PHY_RESET, USB20_PHY1_RESET);
    MmioOr32 (USB20_WRAP_RESET, USB20_WRAP1_RESETS);
    MicroSecondDelay (1000);
    // Force the dedicated host controller; keep the download port alone.
    MmioAndThenOr32 (*Base + 0x0c, ~BIT30, BIT29);
    MicroSecondDelay (25000);
    return EFI_SUCCESS;
  }
  MmioOr32 (USB31_CLOCK, USB31_GATES);
  MmioOr32 (USB_C10_CLOCK, BIT0 | BIT4);
  MmioOr32 (USB31_RESET, USB31_APB_RESET | USB31_AFENCE_RESET);
  MmioAnd32 (USB31_RESET, ~(USB31_CORE_RESET | USB31_PHY_RESET));
  MmioOr32 (USB_C10_RESET, C10_APB_RESET);
  MmioAnd32 (USB_C10_RESET, ~C10_PHY_RESET);
  MicroSecondDelay (100);
  MmioAndThenOr32 (Phy + USB31_OTG_CONTROL,
                   ~USB31_OTG_DISABLE, USB31_VBUS_COMPARE);
  MmioAnd32 (Tca + TCA_CLOCK_RESET, ~TCA_RESET_RELEASE);
  MicroSecondDelay (100);
  MmioOr32 (Tca + TCA_CLOCK_RESET, TCA_RESET_RELEASE);
  MmioAnd32 (Tca + TCA_GLOBAL, ~TCA_MODE_MASK);
  MmioOr32 (Tca + TCA_SYSTEM, TCA_SYSTEM_DISABLE);
  MicroSecondDelay (10);
  MmioAndThenOr32 (Tca + TCA_SYSTEM,
                   ~(TCA_SYSTEM_FLIP | TCA_SYSTEM_CONNECTION),
                   TCA_SYSTEM_CONNECTION);
  MicroSecondDelay (10);
  MmioAnd32 (Tca + TCA_SYSTEM, ~TCA_SYSTEM_DISABLE);
  MmioAndThenOr32 (Tca + TCA_GLOBAL, ~TCA_MODE_MASK, TCA_SYNC_MODE);
  MmioWrite32 (Tca + TCA_IRQ_STATUS, 0xffff);
  MmioWrite32 (Tca + TCA_TCPC, TCA_TCPC_VALID | TCA_TCPC_USB);
  MmioOr32 (USB_C10_RESET, C10_PHY_RESET);
  MmioOr32 (USB31_RESET, USB31_PHY_RESET | USB31_CORE_RESET);
  MicroSecondDelay (1000);
  UINT32 Id = MmioRead32 (*Base + DWC3_GSNPSID) & 0xffff0000U;
  if (Id != 0x33310000U && Id != 0x55330000U)
    return EFI_DEVICE_ERROR;
  MmioOr32 (*Base + DWC3_GCTL, DWC3_CORE_RESET);
  MmioAnd32 (*Base + DWC3_USB2PHYCFG, ~(BIT6 | BIT8));
  MmioAnd32 (*Base + DWC3_USB3PIPECTL, ~BIT17);
  MmioAndThenOr32 (*Base + DWC3_GCTL,
                   ~(DWC3_CORE_RESET | DWC3_PORT_CAP | (3U << 4)),
                   DWC3_HOST);
  return EFI_SUCCESS;
}
