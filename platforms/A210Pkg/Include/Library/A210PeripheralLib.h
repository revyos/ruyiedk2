// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef A210_PERIPHERAL_LIB_H_
#define A210_PERIPHERAL_LIB_H_
#include <Uefi.h>

VOID *A210GetFdt (VOID);
BOOLEAN A210NodeEnabled (CONST VOID *Fdt, INT32 Node);
BOOLEAN A210PropertyIs (CONST VOID *Fdt, INT32 Node,
                        CONST CHAR8 *Name, CONST CHAR8 *Value);
UINT32 A210Cell (CONST VOID *Data);
INT32 A210Reference (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name);
EFI_STATUS A210Reg (CONST VOID *Fdt, INT32 Node, UINTN Index,
                    UINTN *Base, UINTN *Size);
typedef struct {
  UINT8 Address;
  UINT32 ClockHz;
} A210_I2C_DEVICE;
EFI_STATUS A210I2cDevice (CONST VOID *Fdt, INT32 Node,
                          A210_I2C_DEVICE *Device);
EFI_STATUS A210I2cByte (CONST A210_I2C_DEVICE *Device, UINT8 Register,
                        UINT8 *Data, BOOLEAN Read);
EFI_STATUS A210Gpio (CONST VOID *Fdt, CONST UINT32 *Spec,
                     BOOLEAN Output, BOOLEAN Asserted);
EFI_STATUS A210SetSupply (CONST VOID *Fdt, INT32 Node,
                          CONST CHAR8 *Property, BOOLEAN Enable);
EFI_STATUS A210Supply (CONST VOID *Fdt, INT32 Node,
                       CONST CHAR8 *Property);
EFI_STATUS A210GpioHogs (CONST VOID *Fdt);
EFI_STATUS A210Wait32 (UINTN Address, UINT32 Mask, UINT32 Expected,
                       UINTN TimeoutUs);
#endif
