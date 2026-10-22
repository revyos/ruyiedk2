// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef A210_SCMI_LIB_H
#define A210_SCMI_LIB_H

#include <Uefi.h>

EFI_STATUS A210ScmiClocksEnable (CONST VOID *Fdt, INT32 Node);
EFI_STATUS A210ScmiClockEnable (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name);
EFI_STATUS A210ScmiClockGet (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name,
                            UINT64 *Rate);
EFI_STATUS A210ScmiClockSet (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name,
                            UINT64 Rate);
EFI_STATUS A210ScmiReset (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name,
                         BOOLEAN Asserted);
EFI_STATUS A210ScmiResetsDeassert (CONST VOID *Fdt, INT32 Node);
EFI_STATUS A210ScmiPowerOn (CONST VOID *Fdt, INT32 Node);

EFI_STATUS A210ScmiCpuRate (CONST VOID *Fdt, UINTN HartId, UINT64 *Rate);

#endif
