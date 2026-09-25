// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "BoardFdt.h"

typedef struct
{
  INT32 Controller, PadNode;
  UINTN Base, PadBase, Pad;
  UINT32 Pin, Mux;
  UINT8 Address;
  BOOLEAN Expander, AlwaysClocked;
} BOARD_GPIO;

STATIC EFI_STATUS
ReadGpio (CONST VOID *Fdt, CONST UINT32 *Spec, INT32 Bytes,
          BOARD_GPIO *Gpio)
{
  UINT32 Cells, Port, Count = 32, Address;
  INT32 Node, Parent;
  UINTN Size;
  if (Bytes != 12)
    return EFI_COMPROMISED_DATA;
  Node = FdtNodeOffsetByPhandle (Fdt, Cell (Spec));
  if (!Th1520FdtEnabled (Fdt, Node) ||
      EFI_ERROR (ReadCell (Fdt, Node, "#gpio-cells", &Cells)) ||
      Cells != 2 || (Cell (Spec + 2) & ~1U))
    return EFI_UNSUPPORTED;
  Gpio->Pin = Cell (Spec + 1);
  Parent = FdtParentOffset (Fdt, Node);
  Gpio->Controller = Parent;
  Gpio->Expander = Compatible (Fdt, Node, "nxp,pca9557");
  if (Gpio->Expander)
  {
    if (Gpio->Pin >= 8 ||
        EFI_ERROR (ReadCell (Fdt, Node, "reg", &Address)) ||
        Address > 0x7f || !Compatible (Fdt, Parent, "thead,th1520-i2c"))
      return EFI_UNSUPPORTED;
    Gpio->Address = (UINT8)Address;
  }
  else
  {
    if (!Compatible (Fdt, Node, "snps,dw-apb-gpio-port") ||
        !Compatible (Fdt, Parent, "snps,dw-apb-gpio") ||
        EFI_ERROR (ReadCell (Fdt, Node, "reg", &Port)) || Port != 0)
      return EFI_UNSUPPORTED;
    if (Has (Fdt, Node, "ngpios") &&
        EFI_ERROR (ReadCell (Fdt, Node, "ngpios", &Count)))
      return EFI_COMPROMISED_DATA;
    if (!Count || Count > 32 || Gpio->Pin >= Count)
      return EFI_UNSUPPORTED;
    if (EFI_ERROR (GpioPad (Fdt, Node, Gpio->Pin, &Gpio->PadBase,
                            &Gpio->Pad, &Gpio->Mux, &Gpio->PadNode)))
      return EFI_UNSUPPORTED;
  }
  if (EFI_ERROR (Th1520FdtReg (Fdt, Parent, &Gpio->Base, &Size)))
    return EFI_UNSUPPORTED;
  if (Gpio->Expander)
  {
    if (Size < 0xa0 ||
        (Gpio->Base != 0xffe7f20000ULL && Gpio->Base != 0xffe7f24000ULL &&
         Gpio->Base != 0xffe7f28000ULL && Gpio->Base != 0xffec00c000ULL &&
         Gpio->Base != 0xffec014000ULL))
      return EFI_UNSUPPORTED;
    EFI_STATUS Status = PinState (Fdt, Parent, FALSE);
    if (EFI_ERROR (Status))
      return Status;
  }
  else if (Size < 8 || (Gpio->Base != 0xffec005000ULL &&
                        Gpio->Base != 0xffec006000ULL &&
                        Gpio->Base != 0xffe7f34000ULL &&
                        Gpio->Base != 0xffe7f38000ULL &&
                        Gpio->Base != 0xfffff41000ULL &&
                        Gpio->Base != 0xfffff52000ULL))
    return EFI_UNSUPPORTED;
  Gpio->AlwaysClocked =
      !Gpio->Expander &&
      (Gpio->Base == 0xfffff41000ULL || Gpio->Base == 0xfffff52000ULL);
  return Gpio->AlwaysClocked ? EFI_SUCCESS
                             : EnableClock (Fdt, Parent, FALSE);
}

STATIC EFI_STATUS
Regulator (CONST VOID *Fdt, INT32 Node, BOOLEAN Apply)
{
  CONST UINT32 *Spec;
  INT32 Bytes;
  UINT32 Delay = 0;
  BOARD_GPIO Gpio;
  EFI_STATUS Status;
  if (!Th1520FdtEnabled (Fdt, Node) ||
      !Compatible (Fdt, Node, "regulator-fixed") ||
      Has (Fdt, Node, "gpio-open-drain"))
    return EFI_UNSUPPORTED;
  Status = PinState (Fdt, Node, FALSE);
  if (EFI_ERROR (Status))
    return Status;
  if (Has (Fdt, Node, "startup-delay-us") &&
      (EFI_ERROR (ReadCell (Fdt, Node, "startup-delay-us", &Delay)) ||
       Delay > 1000000))
    return EFI_UNSUPPORTED;
  if (Has (Fdt, Node, "gpio") && Has (Fdt, Node, "gpios"))
    return EFI_COMPROMISED_DATA;
  Spec = FdtGetProp (Fdt, Node, Has (Fdt, Node, "gpio") ? "gpio" : "gpios",
                     &Bytes);
  if (!Spec)
  {
    if (!Apply)
      return EFI_SUCCESS;
    Status = PinState (Fdt, Node, TRUE);
    if (!EFI_ERROR (Status) && Delay)
      MicroSecondDelay (Delay);
    return Status;
  }
  Status = ReadGpio (Fdt, Spec, Bytes, &Gpio);
  if (EFI_ERROR (Status) || !Apply)
    return Status;
  if (!Gpio.AlwaysClocked)
  {
    Status = EnableClock (Fdt, Gpio.Controller, TRUE);
    if (EFI_ERROR (Status))
      return Status;
  }
  // The fixed-regulator binding uses enable-active-high for polarity.
  BOOLEAN High = Has (Fdt, Node, "enable-active-high");
  if (Gpio.Expander)
  {
    Status = Th1520FdtPinctrl (Fdt, Gpio.Controller);
    if (!EFI_ERROR (Status))
      Status = Th1520Pca9557Output (Gpio.Base, Gpio.Address,
                                    (UINT8)(1U << Gpio.Pin),
                                    High ? (UINT8)(1U << Gpio.Pin) : 0);
  }
  else
  {
    Status = EnableClock (Fdt, Gpio.PadNode, TRUE);
    if (EFI_ERROR (Status))
      return Status;
    // Latch and direction precede both pin configuration and mux.
    Th1520GpioOutput (Gpio.Base, Gpio.Pin, High);
    Status = PinState (Fdt, Node, TRUE);
    if (!EFI_ERROR (Status))
    {
      UINTN Shift = 4 * (Gpio.Pad % 8);
      MmioAndThenOr32 (Gpio.PadBase + 0x400 + 4 * (Gpio.Pad / 8),
                       ~(15U << Shift), Gpio.Mux << Shift);
    }
  }
  if (!EFI_ERROR (Status) && Delay)
    MicroSecondDelay (Delay);
  return Status;
}

typedef struct
{
  INT32 Nodes[16];
  UINTN Count;
} SUPPLIES;

STATIC EFI_STATUS
Supply (CONST VOID *Fdt, INT32 Consumer, CONST CHAR8 *Property,
        SUPPLIES *Plan, UINTN Depth)
{
  UINT32 Phandle;
  INT32 Node;
  EFI_STATUS Status = ReadCell (Fdt, Consumer, Property, &Phandle);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR (Status) || Depth >= ARRAY_SIZE (Plan->Nodes))
    return EFI_COMPROMISED_DATA;
  Node = FdtNodeOffsetByPhandle (Fdt, Phandle);
  for (UINTN I = 0; I < Plan->Count; I++)
    if (Plan->Nodes[I] == Node)
      return EFI_SUCCESS;
  Status = Regulator (Fdt, Node, FALSE);
  if (EFI_ERROR (Status))
    return Status;
  Status = Supply (Fdt, Node, "vin-supply", Plan, Depth + 1);
  if (EFI_ERROR (Status))
    return Status;
  if (Plan->Count == ARRAY_SIZE (Plan->Nodes))
    return EFI_OUT_OF_RESOURCES;
  Plan->Nodes[Plan->Count++] = Node;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ApplySupplies (CONST VOID *Fdt, SUPPLIES *Plan)
{
  for (UINTN I = 0; I < Plan->Count; I++)
  {
    EFI_STATUS Status = Regulator (Fdt, Plan->Nodes[I], TRUE);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
UsbController (CONST VOID *Fdt, INT32 *Node, UINTN *Base)
{
  UINTN Size;
  *Node = FindEnabled (Fdt, -1, "snps,dwc3");
  if (*Node < 0)
    return EFI_NOT_FOUND;
  if (EFI_ERROR (Th1520FdtReg (Fdt, *Node, Base, &Size)) ||
      *Base != 0xffe7040000ULL || Size < 0x10000)
    return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Fan (CONST VOID *Fdt)
{
  INT32 Node = FindEnabled (Fdt, -1, "pwm-fan");
  INT32 Pwm, Bytes;
  CONST UINT32 *Spec;
  UINT32 Cells, Channel, Period, Flags, Rate, Control;
  UINT64 Cycles;
  UINTN Base, Size;
  SUPPLIES Plan = {0};
  EFI_STATUS Status;
  if (Node < 0)
    return EFI_SUCCESS;
  Spec = FdtGetProp (Fdt, Node, "pwms", &Bytes);
  if (!Spec || Bytes != 16 || !Has (Fdt, Node, "pinctrl-0"))
    return EFI_UNSUPPORTED;
  Pwm = FdtNodeOffsetByPhandle (Fdt, Cell (Spec));
  Channel = Cell (Spec + 1);
  Period = Cell (Spec + 2);
  Flags = Cell (Spec + 3);
  if (!Th1520FdtEnabled (Fdt, Pwm) ||
      !Compatible (Fdt, Pwm, "thead,th1520-pwm") ||
      EFI_ERROR (ReadCell (Fdt, Pwm, "#pwm-cells", &Cells)) ||
      Cells != 3 || Channel >= 6 || !Period || (Flags & ~1U) ||
      EFI_ERROR (Th1520FdtReg (Fdt, Pwm, &Base, &Size)) ||
      Base != 0xffec01c000ULL || Size < 6 * 0x20)
    return EFI_UNSUPPORTED;
  Status = PwmClockRate (Fdt, Pwm, &Rate);
  if (EFI_ERROR (Status))
    return Status;
  Cycles = DivU64x32 (MultU64x32 (Period, Rate), 1000000000);
  if (!Cycles || Cycles > MAX_UINT32)
    return EFI_UNSUPPORTED;
  Status = PinState (Fdt, Node, FALSE);
  if (!EFI_ERROR (Status))
    Status = Supply (Fdt, Node, "fan-supply", &Plan, 0);
  if (!EFI_ERROR (Status))
    Status = ApplySupplies (Fdt, &Plan);
  if (!EFI_ERROR (Status))
    Status = EnableClock (Fdt, Pwm, TRUE);
  if (!EFI_ERROR (Status))
    Status = PinState (Fdt, Node, TRUE);
  if (EFI_ERROR (Status))
    return Status;
  Base += Channel * 0x20;
  Control = BIT5 | (Flags ? 0 : BIT8);
  // Firmware runs the fan at full duty until the OS takes over.
  MmioWrite32 (Base, Control);
  MmioWrite32 (Base + 8, (UINT32)Cycles);
  MmioWrite32 (Base + 12, (UINT32)Cycles);
  MmioWrite32 (Base, Control | BIT2);
  MmioWrite32 (Base, Control | BIT0);
  return EFI_SUCCESS;
}

EFI_STATUS
Th1520FdtEarlyInit (CONST VOID *Fdt)
{
  SUPPLIES Plan = {0};
  INT32 Usb, Node = -1;
  UINTN Base;
  EFI_STATUS FanStatus, Status;
  if (!Fdt || FdtCheckHeader (Fdt))
    return EFI_COMPROMISED_DATA;
  FanStatus = Fan (Fdt);
  Status = UsbController (Fdt, &Usb, &Base);
  if (Status == EFI_NOT_FOUND)
    return FanStatus;
  if (EFI_ERROR (Status))
    return Status;
  while ((Node = FindEnabled (Fdt, Node, "usb-a-connector")) >= 0)
  {
    Status = Supply (Fdt, Node, "vbus-supply", &Plan, 0);
    if (EFI_ERROR (Status))
      return Status;
  }
  Status = ApplySupplies (Fdt, &Plan);
  if (!EFI_ERROR (Status))
    MicroSecondDelay (1000);
  return EFI_ERROR (Status) ? Status : FanStatus;
}

EFI_STATUS
Th1520FdtUsbHostInit (CONST VOID *Fdt, UINTN *Base)
{
  SUPPLIES Plan = {0};
  CONST UINT32 *Phys;
  UINT32 Cells;
  UINTN PhyBase, PhySize;
  INT32 Usb, Node, Bytes, Depth = 0;
  EFI_STATUS Status;
  if (!Fdt || FdtCheckHeader (Fdt))
    return EFI_COMPROMISED_DATA;
  Status = UsbController (Fdt, &Usb, Base);
  if (EFI_ERROR (Status))
    return Status;
  Phys = FdtGetProp (Fdt, Usb, "phys", &Bytes);
  if (!Phys || Bytes != 4)
    return EFI_UNSUPPORTED;
  Node = FdtNodeOffsetByPhandle (Fdt, Cell (Phys));
  if (!Th1520FdtEnabled (Fdt, Node) ||
      !Compatible (Fdt, Node, "thead,th1520-usb-phy") ||
      EFI_ERROR (ReadCell (Fdt, Node, "#phy-cells", &Cells)) || Cells)
    return EFI_UNSUPPORTED;
  if (EFI_ERROR (Th1520FdtReg (Fdt, Node, &PhyBase, &PhySize)) ||
      PhyBase != 0xffec030000ULL || PhySize < 0xf038)
    return EFI_UNSUPPORTED;
  Status = Supply (Fdt, Node, "avdd33-usb3-supply", &Plan, 0);
  if (EFI_ERROR (Status))
    return Status;
  Node = Usb;
  while ((Node = FdtNextNode (Fdt, Node, &Depth)) >= 0 && Depth > 0)
    if (Th1520FdtEnabled (Fdt, Node))
    {
      Status = Supply (Fdt, Node, "vdd-supply", &Plan, 0);
      if (EFI_ERROR (Status))
        return Status;
    }
  return ApplySupplies (Fdt, &Plan);
}
