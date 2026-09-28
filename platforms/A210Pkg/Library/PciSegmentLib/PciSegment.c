// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210PciLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PciSegmentLib.h>
#include <Library/UefiBootServicesTableLib.h>

RETURN_STATUS EFIAPI
PciSegmentRegisterForRuntimeAccess (UINTN Address)
{
  return RETURN_UNSUPPORTED;
}

// Serialize read-modify-write operations as well as iATU selection.
#define PCI_ACCESS(Bits, Type)                                           \
Type EFIAPI PciSegmentRead##Bits (UINT64 Address)                        \
{                                                                        \
  ASSERT ((Address & (sizeof (Type) - 1)) == 0);                         \
  return (Type)A210PciRead (Address, sizeof (Type));                     \
}                                                                        \
Type EFIAPI PciSegmentWrite##Bits (UINT64 Address, Type Value)           \
{                                                                        \
  ASSERT ((Address & (sizeof (Type) - 1)) == 0);                         \
  A210PciWrite (Address, sizeof (Type), Value);                          \
  return Value;                                                          \
}                                                                        \
Type EFIAPI PciSegmentAndThenOr##Bits (UINT64 Address,                   \
                                      Type And, Type Or)                 \
{                                                                        \
  EFI_TPL Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);                          \
  Type Value = (PciSegmentRead##Bits (Address) & And) | Or;              \
  PciSegmentWrite##Bits (Address, Value);                                \
  gBS->RestoreTPL (Tpl);                                                 \
  return Value;                                                          \
}                                                                        \
Type EFIAPI PciSegmentOr##Bits (UINT64 Address, Type Or)                 \
{                                                                        \
  return PciSegmentAndThenOr##Bits (Address, (Type)~0U, Or);             \
}                                                                        \
Type EFIAPI PciSegmentAnd##Bits (UINT64 Address, Type And)               \
{                                                                        \
  return PciSegmentAndThenOr##Bits (Address, And, 0);                    \
}                                                                        \
Type EFIAPI PciSegmentBitFieldRead##Bits (UINT64 Address,                \
                                         UINTN Start, UINTN End)         \
{                                                                        \
  return BitFieldRead##Bits (PciSegmentRead##Bits (Address), Start, End);\
}                                                                        \
Type EFIAPI PciSegmentBitFieldWrite##Bits (UINT64 Address,               \
    UINTN Start, UINTN End, Type Value)                                  \
{                                                                        \
  EFI_TPL Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);                          \
  Value = BitFieldWrite##Bits (PciSegmentRead##Bits (Address),           \
                               Start, End, Value);                       \
  PciSegmentWrite##Bits (Address, Value);                                \
  gBS->RestoreTPL (Tpl);                                                 \
  return Value;                                                          \
}                                                                        \
Type EFIAPI PciSegmentBitFieldAndThenOr##Bits (UINT64 Address,           \
    UINTN Start, UINTN End, Type And, Type Or)                           \
{                                                                        \
  EFI_TPL Tpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);                          \
  Type Value = BitFieldAndThenOr##Bits (PciSegmentRead##Bits (Address),  \
                                        Start, End, And, Or);            \
  PciSegmentWrite##Bits (Address, Value);                                \
  gBS->RestoreTPL (Tpl);                                                 \
  return Value;                                                          \
}                                                                        \
Type EFIAPI PciSegmentBitFieldOr##Bits (UINT64 Address,                  \
    UINTN Start, UINTN End, Type Or)                                     \
{                                                                        \
  Type Mask = BitFieldRead##Bits ((Type)~0U, Start, End);                \
  return PciSegmentBitFieldAndThenOr##Bits (                             \
      Address, Start, End, Mask, Or);                                   \
}                                                                        \
Type EFIAPI PciSegmentBitFieldAnd##Bits (UINT64 Address,                 \
    UINTN Start, UINTN End, Type And)                                    \
{                                                                        \
  return PciSegmentBitFieldAndThenOr##Bits (Address, Start, End, And, 0);\
}

PCI_ACCESS (8, UINT8)
PCI_ACCESS (16, UINT16)
PCI_ACCESS (32, UINT32)

STATIC UINTN
Transfer (UINT64 Address, UINTN Size, VOID *Buffer, BOOLEAN Write)
{
  UINTN Left = Size;
  UINT8 *Bytes = Buffer;
  ASSERT (Size <= 4096 - (Address & 0xfff));
  ASSERT (Buffer != NULL || Size == 0);
  if (Size > 4096 - (Address & 0xfff) || (!Buffer && Size))
    return 0;
  while (Left)
  {
    UINTN Width = !(Address & 3) && Left >= 4 ? 4
                      : (!(Address & 1) && Left >= 2 ? 2 : 1);
    UINT32 Value = 0;
    if (Write)
    {
      CopyMem (&Value, Bytes, Width);
      A210PciWrite (Address, Width, Value);
    }
    else
    {
      Value = A210PciRead (Address, Width);
      CopyMem (Bytes, &Value, Width);
    }
    Address += Width;
    Bytes += Width;
    Left -= Width;
  }
  return Size;
}

UINTN EFIAPI
PciSegmentReadBuffer (UINT64 Address, UINTN Size, VOID *Buffer)
{
  return Transfer (Address, Size, Buffer, FALSE);
}

UINTN EFIAPI
PciSegmentWriteBuffer (UINT64 Address, UINTN Size, VOID *Buffer)
{
  return Transfer (Address, Size, (VOID *)Buffer, TRUE);
}
