// SPDX-License-Identifier: GPL-2.0-only
// A210 USB integration, following the Linux A210 PHY drivers.
#include <Uefi.h>
#include <A210.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210ScmiLib.h>
#include <Library/A210UsbHostLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include "Typec.h"

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
#define TCA_TCPC_LOW_POWER BIT3
#define TCA_TCPC_FLIP BIT2
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

STATIC UINTN mTca;

VOID
A210UsbOrientation (FUSB302_ORIENTATION Orientation)
{
  MmioAnd32 (mTca + TCA_GLOBAL, ~TCA_MODE_MASK);
  MmioOr32 (mTca + TCA_SYSTEM, TCA_SYSTEM_DISABLE);
  MicroSecondDelay (10);
  MmioAndThenOr32 (mTca + TCA_SYSTEM,
                   ~(TCA_SYSTEM_FLIP | TCA_SYSTEM_CONNECTION),
                   TCA_SYSTEM_CONNECTION
                   | (Orientation == Fusb302Cc2 ? TCA_SYSTEM_FLIP : 0));
  MicroSecondDelay (10);
  MmioAnd32 (mTca + TCA_SYSTEM, ~TCA_SYSTEM_DISABLE);
  MmioAndThenOr32 (mTca + TCA_GLOBAL, ~TCA_MODE_MASK, TCA_SYNC_MODE);
  MmioWrite32 (mTca + TCA_IRQ_STATUS, 0xffff);
  MmioWrite32 (mTca + TCA_TCPC, TCA_TCPC_VALID
                | (Orientation == Fusb302Detached ? TCA_TCPC_LOW_POWER
                                                   : TCA_TCPC_USB)
                | (Orientation == Fusb302Cc2 ? TCA_TCPC_FLIP : 0));
}

EFI_STATUS
A210UsbHostPoll (VOID)
{
  return A210TypecPoll ();
}

VOID
A210UsbHostStop (VOID)
{
  A210TypecStop ();
}

STATIC EFI_STATUS
PhyRegisters (CONST VOID *Fdt, INT32 Node, UINTN Index,
              CONST CHAR8 *Compatible, BOOLEAN Switched, UINTN *Base,
              INT32 *PhyNode)
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
  if (!Switched && FdtGetProp (Fdt, Phy, "orientation-switch", NULL))
    return EFI_UNSUPPORTED;
  *PhyNode = Phy;
  return A210Reg (Fdt, Phy, 0, Base, &Size);
}

EFI_STATUS
A210UsbHostInit (BOOLEAN Usb3, UINTN *Base)
{
  VOID *Fdt = A210GetFdt ();
  INT32 Node = -1, Usb2Phy, Usb3Phy;
  UINTN Size, Phy, Tca;
  EFI_STATUS Status;
  BOOLEAN Typec;
  CONST CHAR8 *Compatible = Usb3 ? "zhihe,a210-dwc3"
                                 : "zhihe,a210-usb";
  if (!Fdt || !Base)
    return EFI_INVALID_PARAMETER;
  while ((Node = FdtNodeOffsetByCompatible (Fdt, Node, Compatible)) >= 0)
    if (A210NodeEnabled (Fdt, Node)
        && (A210PropertyIs (Fdt, Node, "dr_mode", "host")
            || (Usb3 && A210PropertyIs (Fdt, Node, "dr_mode", "otg")
                && FdtGetProp (Fdt, Node, "usb-role-switch", NULL))))
      break;
  if (Node < 0)
    return EFI_NOT_FOUND;
  Typec = Usb3 && FdtGetProp (Fdt, Node, "usb-role-switch", NULL);
  Status = A210Reg (Fdt, Node, 0, Base, &Size);
  if (EFI_ERROR (Status) || Size < 0x10000
      || *Base != (Usb3 ? A210_USB3_BASE : A210_USB2_HOST_BASE))
    return EFI_UNSUPPORTED;
  Status = PhyRegisters (Fdt, Node, 0,
      Usb3 ? "zhihe,a210-usb31-usb2-phy" : "zhihe,a210-usb2-phy1",
      FALSE, &Phy, &Usb2Phy);
  if (EFI_ERROR (Status))
    return Status;
  if (Phy != (Usb3 ? (A210_USB31_SYSREG_BASE + 0x80)
                  : A210_USB2_PHY1_BASE))
    return EFI_UNSUPPORTED;
  if (Usb3)
  {
    Status = PhyRegisters (Fdt, Node, 1, "zhihe,a210-c10-phy",
                           Typec, &Tca, &Usb3Phy);
    if (EFI_ERROR (Status) || Tca != A210_USB_C10_BASE)
      return EFI_UNSUPPORTED;
    mTca = Tca;
    if (Typec)
    {
      CONST UINT32 *Phys = FdtGetProp (Fdt, Node, "phys", NULL);
      INT32 PhyNode = FdtNodeOffsetByPhandle (Fdt, A210Cell (Phys + 1));
      Status = A210TypecInit (Fdt, Node, PhyNode);
      if (EFI_ERROR (Status))
        return Status;
    }
  }
  Status = A210GpioHogs (Fdt);
  if (EFI_ERROR (Status))
    return Status;
  Status = Typec ? EFI_SUCCESS : A210Supply (Fdt, Node, "vbus-supply");
  // Explicit fixed-host DTs may provide VBUS through a GPIO hog.
  if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
    return Status;
  Status = A210ScmiPowerOn (Fdt, Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Fdt, Node);
  if (!EFI_ERROR (Status))
    Status = A210ScmiClocksEnable (Fdt, Usb2Phy);
  if (EFI_ERROR (Status))
    return Status;
  if (!Usb3)
  {
    Status = A210ScmiReset (Fdt, Node, "dwc2", FALSE);
    if (!EFI_ERROR (Status))
      Status = A210ScmiReset (Fdt, Node, "dwc2", TRUE);
    if (EFI_ERROR (Status))
      return Status;
    MicroSecondDelay (10);
    Status = A210ScmiReset (Fdt, Node, "dwc2", FALSE);
    if (!EFI_ERROR (Status))
      Status = A210ScmiReset (Fdt, Usb2Phy, "phy", TRUE);
    if (EFI_ERROR (Status))
      return Status;
    MicroSecondDelay (100);
    MmioAndThenOr32 (Phy + PHY_ANALOG, ~PHY_TX_LEVEL_MASK, PHY_TX_LEVEL);
    MmioOr32 (Phy + PHY_CONFIG, PHY1_PULLDOWNS);
    Status = A210ScmiReset (Fdt, Usb2Phy, "phy", FALSE);
    if (EFI_ERROR (Status))
      return Status;
    MicroSecondDelay (1000);
    // Force the dedicated host controller; keep the download port alone.
    MmioAndThenOr32 (*Base + 0x0c, ~BIT30, BIT29);
    MicroSecondDelay (25000);
    return EFI_SUCCESS;
  }
  Status = A210ScmiClocksEnable (Fdt, Usb3Phy);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Node, NULL, TRUE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Usb2Phy, "phy", TRUE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Usb3Phy, "apb", FALSE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Usb3Phy, "phy", TRUE);
  if (EFI_ERROR (Status))
    return Status;
  MicroSecondDelay (100);
  MmioAndThenOr32 (Phy + USB31_OTG_CONTROL,
                   ~USB31_OTG_DISABLE, USB31_VBUS_COMPARE);
  MmioAnd32 (Tca + TCA_CLOCK_RESET, ~TCA_RESET_RELEASE);
  MicroSecondDelay (100);
  MmioOr32 (Tca + TCA_CLOCK_RESET, TCA_RESET_RELEASE);
  A210UsbOrientation (Typec ? Fusb302Detached : Fusb302Cc1);
  Status = A210ScmiReset (Fdt, Usb3Phy, "phy", FALSE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Usb2Phy, "phy", FALSE);
  if (!EFI_ERROR (Status))
    Status = A210ScmiReset (Fdt, Node, NULL, FALSE);
  if (EFI_ERROR (Status))
    return Status;
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
