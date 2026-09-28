// SPDX-License-Identifier: BSD-2-Clause-Patent
// CPU I/O for PCI hosts whose port-I/O windows are memory mapped.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/CpuIo2.h>

STATIC EFI_STATUS
Transfer (EFI_CPU_IO_PROTOCOL_WIDTH Width, UINT64 Address,
          UINTN Count, VOID *Buffer, BOOLEAN Write, BOOLEAN PortIo)
{
  UINTN Size, AddressStep, BufferStep;
  UINT8 *Bytes = Buffer;
  if ((UINT32)Width >= EfiCpuIoWidthMaximum || !Buffer)
    return EFI_INVALID_PARAMETER;
  Size = 1U << (Width & 3);
  if ((Address & (Size - 1)) || ((UINTN)Buffer & (Size - 1))
      || Address > MAX_UINTN || (PortIo && Size == 8))
    return EFI_UNSUPPORTED;
  AddressStep = Width >= EfiCpuIoWidthFifoUint8
                   && Width <= EfiCpuIoWidthFifoUint64 ? 0 : Size;
  BufferStep = Width >= EfiCpuIoWidthFillUint8 ? 0 : Size;
  if (Count && (Address > MAX_UINTN - Size + 1
      || (AddressStep && Count - 1 > (MAX_UINTN - Address) / Size)
      || (BufferStep && Count > (MAX_UINTN - (UINTN)Buffer) / Size)))
    return EFI_UNSUPPORTED;
  MemoryFence ();
  while (Count--)
  {
    UINT64 Value = 0;
    if (Write)
    {
      CopyMem (&Value, Bytes, Size);
      switch (Size)
      {
      case 1: MmioWrite8 (Address, Value); break;
      case 2: MmioWrite16 (Address, Value); break;
      case 4: MmioWrite32 (Address, Value); break;
      case 8: MmioWrite64 (Address, Value); break;
      }
    }
    else
    {
      switch (Size)
      {
      case 1: Value = MmioRead8 (Address); break;
      case 2: Value = MmioRead16 (Address); break;
      case 4: Value = MmioRead32 (Address); break;
      case 8: Value = MmioRead64 (Address); break;
      }
      CopyMem (Bytes, &Value, Size);
    }
    Address += AddressStep;
    Bytes += BufferStep;
  }
  MemoryFence ();
  return EFI_SUCCESS;
}

#define IO_ACCESS(Name, Write, Port)                                     \
STATIC EFI_STATUS EFIAPI Name (EFI_CPU_IO2_PROTOCOL *This,               \
    EFI_CPU_IO_PROTOCOL_WIDTH Width, UINT64 Address,                     \
    UINTN Count, VOID *Buffer)                                          \
{                                                                      \
  return Transfer (Width, Address, Count, Buffer, Write, Port);           \
}
IO_ACCESS (MemoryRead, FALSE, FALSE)
IO_ACCESS (MemoryWrite, TRUE, FALSE)
IO_ACCESS (PortRead, FALSE, TRUE)
IO_ACCESS (PortWrite, TRUE, TRUE)

STATIC EFI_CPU_IO2_PROTOCOL mCpuIo = {
  {MemoryRead, MemoryWrite}, {PortRead, PortWrite}
};

EFI_STATUS EFIAPI
MmioCpuIoInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE Handle = NULL;
  return gBS->InstallProtocolInterface (
      &Handle, &gEfiCpuIo2ProtocolGuid, EFI_NATIVE_INTERFACE, &mCpuIo);
}
