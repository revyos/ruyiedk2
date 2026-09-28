// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Guid/FdtHob.h>
#include <Library/A210PeripheralLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

VOID *
A210GetFdt (VOID)
{
  VOID *Hob = GetFirstGuidHob (&gFdtHobGuid);
  if (!Hob || GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))
    return NULL;
  return (VOID *)(UINTN)*(UINT64 *)GET_GUID_HOB_DATA (Hob);
}

UINT32
A210Cell (CONST VOID *Data)
{
  return SwapBytes32 (ReadUnaligned32 (Data));
}

BOOLEAN
A210PropertyIs (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name,
                CONST CHAR8 *Value)
{
  INT32 Length;
  CONST VOID *Data = FdtGetProp (Fdt, Node, Name, &Length);
  return Data && Length == AsciiStrSize (Value)
         && CompareMem (Data, Value, Length) == 0;
}

BOOLEAN
A210NodeEnabled (CONST VOID *Fdt, INT32 Node)
{
  if (Node < 0)
    return FALSE;
  for (; Node >= 0; Node = FdtParentOffset (Fdt, Node))
    if (FdtGetProp (Fdt, Node, "status", NULL)
        && !A210PropertyIs (Fdt, Node, "status", "okay")
        && !A210PropertyIs (Fdt, Node, "status", "ok"))
      return FALSE;
  return TRUE;
}

INT32
A210Reference (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Length;
  CONST UINT32 *Data = FdtGetProp (Fdt, Node, Name, &Length);
  return Data && Length == 4
             ? FdtNodeOffsetByPhandle (Fdt, A210Cell (Data)) : -1;
}

EFI_STATUS
A210Reg (CONST VOID *Fdt, INT32 Node, UINTN Index,
         UINTN *Base, UINTN *Size)
{
  INT32 Length, Parent = FdtParentOffset (Fdt, Node);
  CONST UINT64 *Data = FdtGetProp (Fdt, Node, "reg", &Length);
  CONST UINT32 *Cells;
  UINT64 Address, Bytes;
  Cells = FdtGetProp (Fdt, Parent, "#address-cells", NULL);
  if (!Cells || A210Cell (Cells) != 2)
    return EFI_UNSUPPORTED;
  Cells = FdtGetProp (Fdt, Parent, "#size-cells", NULL);
  if (!Cells || A210Cell (Cells) != 2 || !Data || Length < 16
      || Length % 16 || Index >= (UINTN)Length / 16)
    return EFI_UNSUPPORTED;
  Address = SwapBytes64 (ReadUnaligned64 (Data + Index * 2));
  Bytes = SwapBytes64 (ReadUnaligned64 (Data + Index * 2 + 1));
  if (!Bytes || Address > MAX_UINTN - Bytes)
    return EFI_UNSUPPORTED;
  *Base = Address;
  *Size = Bytes;
  return EFI_SUCCESS;
}

EFI_STATUS
A210Wait32 (UINTN Address, UINT32 Mask, UINT32 Expected, UINTN TimeoutUs)
{
  while ((MmioRead32 (Address) & Mask) != Expected)
  {
    if (TimeoutUs < 10)
      return EFI_TIMEOUT;
    MicroSecondDelay (10);
    TimeoutUs -= 10;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
SetSupply (CONST VOID *Fdt, INT32 Node, BOOLEAN Enable, UINTN Depth)
{
  CONST UINT32 *Gpio, *Delay;
  INT32 Parent, Length;
  EFI_STATUS Status;
  if (Depth > 8 || !A210NodeEnabled (Fdt, Node)
      || !A210PropertyIs (Fdt, Node, "compatible", "regulator-fixed"))
    return EFI_UNSUPPORTED;
  if (!Enable && FdtGetProp (Fdt, Node, "regulator-always-on", NULL))
    return EFI_ACCESS_DENIED;
  Parent = A210Reference (Fdt, Node, "vin-supply");
  if (Enable && Parent >= 0)
  {
    Status = SetSupply (Fdt, Parent, TRUE, Depth + 1);
    if (EFI_ERROR (Status))
      return Status;
  }
  Gpio = FdtGetProp (Fdt, Node, "gpio", &Length);
  if (Gpio)
  {
    UINT32 Spec[3];
    if (Length != sizeof (Spec))
      return EFI_UNSUPPORTED;
    CopyMem (Spec, Gpio, sizeof (Spec));
    // Fixed regulators use enable-active-high for GPIO polarity.
    Spec[2] = SwapBytes32 (
        FdtGetProp (Fdt, Node, "enable-active-high", NULL) ? 0 : 1);
    Status = A210Gpio (Fdt, Spec, TRUE, Enable);
    if (EFI_ERROR (Status))
      return Status;
  }
  if (!Enable)
    return Gpio ? EFI_SUCCESS : EFI_UNSUPPORTED;
  Delay = FdtGetProp (Fdt, Node, "startup-delay-us", &Length);
  if (Delay && Length == 4)
  {
    if (A210Cell (Delay) > 1000000)
      return EFI_UNSUPPORTED;
    MicroSecondDelay (A210Cell (Delay));
  }
  return EFI_SUCCESS;
}

EFI_STATUS
A210Supply (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Property)
{
  return A210SetSupply (Fdt, Node, Property, TRUE);
}

EFI_STATUS
A210SetSupply (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Property,
               BOOLEAN Enable)
{
  INT32 Supply = A210Reference (Fdt, Node, Property);
  return Supply < 0 ? EFI_NOT_FOUND : SetSupply (Fdt, Supply, Enable, 0);
}
