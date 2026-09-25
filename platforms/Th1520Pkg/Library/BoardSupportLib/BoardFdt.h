// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/Th1520BoardSupportLib.h>

STATIC inline UINT32
Cell (CONST VOID *P)
{
  return SwapBytes32 (ReadUnaligned32 (P));
}
STATIC inline BOOLEAN
Has (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  return FdtGetProp (Fdt, Node, Name, NULL) != NULL;
}
STATIC inline BOOLEAN
Compatible (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  return FdtStringListSearch (Fdt, Node, "compatible", Name) >= 0;
}
BOOLEAN Th1520FdtEnabled (CONST VOID *Fdt, INT32 Node);
EFI_STATUS Th1520FdtReg (
    CONST VOID *Fdt, INT32 Node, UINTN *Base, UINTN *Size);
EFI_STATUS
ReadCell (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT32 *Value);
INT32
FindEnabled (CONST VOID *Fdt, INT32 After, CONST CHAR8 *Name);
EFI_STATUS
EnableClock (CONST VOID *Fdt, INT32 Node, BOOLEAN Apply);
EFI_STATUS
PwmClockRate (CONST VOID *Fdt, INT32 Node, UINT32 *Rate);
EFI_STATUS
PinState (CONST VOID *Fdt, INT32 Node, BOOLEAN Apply);
EFI_STATUS
GpioPad (CONST VOID *Fdt, INT32 Port, UINT32 Pin, UINTN *Base, UINTN *Pad,
         UINT32 *Mux, INT32 *PadNode);
EFI_STATUS
PadController (CONST VOID *Fdt, INT32 Node, UINTN *Base, UINT32 *Group);
