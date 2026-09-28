// SPDX-License-Identifier: GPL-2.0-only
#include <Uefi.h>
#include <Library/A210PeripheralLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include "Typec.h"

STATIC CONST VOID *mFdt;
STATIC INT32 mTcpc = -1;
STATIC FUSB302 mPort;
STATIC A210_I2C_DEVICE mDevice;
STATIC FUSB302_ORIENTATION mOrientation;

STATIC INT32
Connector (CONST VOID *Fdt, INT32 Node)
{
  INT32 Child = Node, Peer, Depth = 0;
  CONST UINT32 *Remote;
  INT32 Length;

  // Follow the graph instead of choosing an unrelated TCPC by address.
  while ((Child = FdtNextNode (Fdt, Child, &Depth)) >= 0 && Depth > 0)
  {
    Remote = FdtGetProp (Fdt, Child, "remote-endpoint", &Length);
    if (!Remote || Length != 4)
      continue;
    Peer = FdtNodeOffsetByPhandle (Fdt, A210Cell (Remote));
    Remote = FdtGetProp (Fdt, Peer, "remote-endpoint", &Length);
    if (!Remote || Length != 4
        || A210Cell (Remote) != FdtGetPhandle (Fdt, Child))
      return -1;
    for (; Peer >= 0; Peer = FdtParentOffset (Fdt, Peer))
      if (A210PropertyIs (Fdt, Peer, "compatible", "usb-c-connector"))
        return Peer;
  }
  return -1;
}

STATIC EFI_STATUS
Transfer (VOID *Context, UINT8 Register, UINT8 *Value, BOOLEAN Read)
{
  return A210I2cByte (&mDevice, Register, Value, Read);
}

EFI_STATUS
A210TypecInit (CONST VOID *Fdt, INT32 Controller, INT32 Phy)
{
  INT32 Port = Connector (Fdt, Controller);
  EFI_STATUS Status;

  if (Port < 0 || Port != Connector (Fdt, Phy)
      || !A210NodeEnabled (Fdt, Port)
      || !(A210PropertyIs (Fdt, Port, "data-role", "host")
           || A210PropertyIs (Fdt, Port, "data-role", "dual"))
      || !(A210PropertyIs (Fdt, Port, "power-role", "source")
           || A210PropertyIs (Fdt, Port, "power-role", "dual")))
    return EFI_UNSUPPORTED;
  mTcpc = FdtParentOffset (Fdt, Port);
  if (!A210NodeEnabled (Fdt, mTcpc)
      || !A210PropertyIs (Fdt, mTcpc, "compatible", "fcs,fusb302"))
  {
    mTcpc = -1;
    return EFI_UNSUPPORTED;
  }
  mFdt = Fdt;
  mPort.Transfer = Transfer;
  Status = A210SetSupply (Fdt, mTcpc, "vbus-supply", FALSE);
  if (!EFI_ERROR (Status))
    Status = A210I2cDevice (Fdt, mTcpc, &mDevice);
  if (!EFI_ERROR (Status))
    Status = Fusb302Init (&mPort);
  if (EFI_ERROR (Status))
    mTcpc = -1;
  return Status;
}

EFI_STATUS
A210TypecPoll (VOID)
{
  FUSB302_ORIENTATION Orientation;
  EFI_STATUS Status;

  if (mTcpc < 0)
    return EFI_NOT_FOUND;
  Status = Fusb302Poll (&mPort, &Orientation);
  if (EFI_ERROR (Status))
    Orientation = Fusb302Detached;
  if (Orientation == mOrientation)
    return Status;
  if (mOrientation != Fusb302Detached)
  {
    Status = A210SetSupply (mFdt, mTcpc, "vbus-supply", FALSE);
    A210UsbOrientation (Fusb302Detached);
    if (EFI_ERROR (Status))
      return Status;
    mOrientation = Fusb302Detached;
  }
  if (Orientation != Fusb302Detached)
  {
    A210UsbOrientation (Orientation);
    Status = A210SetSupply (mFdt, mTcpc, "vbus-supply", TRUE);
    if (EFI_ERROR (Status))
    {
      A210UsbOrientation (Fusb302Detached);
      mPort.Orientation = Fusb302Detached;
      return Status;
    }
    mOrientation = Orientation;
  }
  DEBUG ((DEBUG_INFO, "A210 Type-C: orientation %u\n", mOrientation));
  return Status;
}

VOID
A210TypecStop (VOID)
{
  if (mTcpc >= 0)
    A210SetSupply (mFdt, mTcpc, "vbus-supply", FALSE);
}
