// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/A210PeripheralLib.h>
#include <Library/A210ScmiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define PAD_MUX 0x400
#define GPIO_DATA 0x00
#define GPIO_DIRECTION 0x04
#define PCA_OUTPUT 2
#define PCA_DIRECTION 6

STATIC VOID
PadMux (UINTN Base, UINTN Pin)
{
  UINTN Shift = Pin % 8 * 4;
  MmioAnd32 (Base + PAD_MUX + Pin / 8 * 4, ~(0xfU << Shift));
}

STATIC EFI_STATUS
ExpanderGpio (CONST VOID *Fdt, INT32 Node, UINT32 Pin,
              BOOLEAN Output, BOOLEAN High)
{
  UINT8 Value, Mask;
  A210_I2C_DEVICE Device;
  EFI_STATUS Status;
  if (Pin >= 16)
    return EFI_UNSUPPORTED;
  Status = A210I2cDevice (Fdt, Node, &Device);
  if (EFI_ERROR (Status))
    return Status;
  Mask = 1U << (Pin % 8);
  if (Output)
  {
    Status = A210I2cByte (&Device, PCA_OUTPUT + Pin / 8, &Value, TRUE);
    if (EFI_ERROR (Status))
      return Status;
    Value = (Value & ~Mask) | (High ? Mask : 0);
    Status = A210I2cByte (&Device, PCA_OUTPUT + Pin / 8, &Value, FALSE);
    if (EFI_ERROR (Status))
      return Status;
  }
  Status = A210I2cByte (&Device, PCA_DIRECTION + Pin / 8, &Value, TRUE);
  if (EFI_ERROR (Status))
    return Status;
  Value = (Value & ~Mask) | (Output ? 0 : Mask);
  return A210I2cByte (&Device, PCA_DIRECTION + Pin / 8, &Value, FALSE);
}

STATIC EFI_STATUS
SetGpio (CONST VOID *Fdt, CONST UINT32 *Spec,
          BOOLEAN Output, BOOLEAN Asserted)
{
  INT32 Node = FdtNodeOffsetByPhandle (Fdt, A210Cell (Spec));
  UINT32 Pin = A210Cell (Spec + 1), Flags = A210Cell (Spec + 2);
  STATIC BOOLEAN Prepared[2];
  UINTN Base, Size, Pad, PadBase, Group;
  CONST UINT32 *Ranges;
  INT32 Length, PadNode, Bus;
  EFI_STATUS Status;
  BOOLEAN High = Asserted != ((Flags & 1) != 0);
  if (!A210NodeEnabled (Fdt, Node) || Flags & ~1U)
    return EFI_UNSUPPORTED;
  if (A210PropertyIs (Fdt, Node, "compatible", "nxp,pca9535")
      || A210PropertyIs (Fdt, Node, "compatible", "awinic,aw9535"))
    return ExpanderGpio (Fdt, Node, Pin, Output, High);
  if (Pin >= 32 || EFI_ERROR (A210Reg (
      Fdt, FdtParentOffset (Fdt, Node), 0, &Base, &Size)))
    return EFI_UNSUPPORTED;
  Pad = Pin;
  if (Base == A210_GPIO0_BASE)
  {
    PadBase = A210_PERI1_PAD_BASE;
  }
  else if (Base == A210_AON_GPIO_BASE && (Pin == 26 || Pin == 29))
  {
    PadBase = A210_AON_PAD_BASE;
    Pad++;
  }
  else
    return EFI_UNSUPPORTED;
  Bus = FdtParentOffset (Fdt, Node);
  Ranges = FdtGetProp (Fdt, Node, "gpio-ranges", &Length);
  if (!Ranges || Length != 16)
    return EFI_UNSUPPORTED;
  PadNode = FdtNodeOffsetByPhandle (Fdt, A210Cell (Ranges));
  if (!A210NodeEnabled (Fdt, PadNode))
    return EFI_UNSUPPORTED;
  Group = Base == A210_AON_GPIO_BASE;
  if (!Prepared[Group])
  {
    Status = A210ScmiClocksEnable (Fdt, Bus);
    if (!EFI_ERROR (Status))
      Status = A210ScmiResetsDeassert (Fdt, Bus);
    if (!EFI_ERROR (Status))
      Status = A210ScmiClocksEnable (Fdt, PadNode);
    if (!EFI_ERROR (Status))
      Status = A210ScmiResetsDeassert (Fdt, PadNode);
    if (EFI_ERROR (Status))
      return Status;
    Prepared[Group] = TRUE;
  }
  if (Output)
    MmioAndThenOr32 (Base + GPIO_DATA, ~(1U << Pin),
                     High ? 1U << Pin : 0);
  MmioAndThenOr32 (Base + GPIO_DIRECTION, ~(1U << Pin),
                   Output ? 1U << Pin : 0);
  PadMux (PadBase, Pad);
  if (!Output)
    MmioOr32 (PadBase + Pad / 2 * 4, BIT9 << (Pad % 2 * 16));
  return EFI_SUCCESS;
}

EFI_STATUS
A210Gpio (CONST VOID *Fdt, CONST UINT32 *Spec,
          BOOLEAN Output, BOOLEAN Asserted)
{
  EFI_TPL Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  EFI_STATUS Status = SetGpio (Fdt, Spec, Output, Asserted);
  gBS->RestoreTPL (Tpl);
  return Status;
}

EFI_STATUS
A210GpioHogs (CONST VOID *Fdt)
{
  INT32 Node = -1, Depth = 0, Child, Length;
  EFI_STATUS Status;
  while ((Node = FdtNextNode (Fdt, Node, &Depth)) >= 0)
  {
    CONST UINT32 *Phandle;
    if (!A210NodeEnabled (Fdt, Node)
        || !FdtGetProp (Fdt, Node, "gpio-controller", NULL))
      continue;
    Phandle = FdtGetProp (Fdt, Node, "phandle", &Length);
    if (!Phandle || Length != 4)
      continue;
    FdtForEachSubnode (Child, Fdt, Node)
    {
      CONST UINT32 *Pins;
      UINT32 Spec[3];
      BOOLEAN Input, High;
      if (!FdtGetProp (Fdt, Child, "gpio-hog", NULL)
          || !A210NodeEnabled (Fdt, Child))
        continue;
      Pins = FdtGetProp (Fdt, Child, "gpios", &Length);
      if (!Pins || Length != 8)
        return EFI_UNSUPPORTED;
      Spec[0] = *Phandle;
      CopyMem (Spec + 1, Pins, 8);
      Input = FdtGetProp (Fdt, Child, "input", NULL) != NULL;
      High = FdtGetProp (Fdt, Child, "output-high", NULL) != NULL;
      if (!Input && !High
          && !FdtGetProp (Fdt, Child, "output-low", NULL))
        return EFI_UNSUPPORTED;
      Status = A210Gpio (Fdt, Spec, !Input, High);
      if (EFI_ERROR (Status))
        return Status;
    }
  }
  return EFI_SUCCESS;
}
