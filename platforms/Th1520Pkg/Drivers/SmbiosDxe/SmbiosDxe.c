// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Library/PcdLib.h>
#include <Library/FdtLib.h>
#include <Guid/FdtHob.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Smbios.h>

STATIC EFI_STATUS
Add (EFI_SMBIOS_PROTOCOL *Smbios, VOID *Record, UINTN Size, CONST CHAR8 *Strings, UINTN StringSize,
     EFI_SMBIOS_HANDLE *Handle)
{
  EFI_STATUS Status;
  UINT8 *Buffer = AllocateZeroPool (Size + StringSize + 2);
  if (Buffer == NULL)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Buffer, Record, Size);
  CopyMem (Buffer + Size, Strings, StringSize);
  ((SMBIOS_STRUCTURE *)Buffer)->Length = (UINT8)Size;
  *Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->Add (Smbios, NULL, Handle, (VOID *)Buffer);
  FreePool (Buffer);
  return Status;
}
STATIC UINT16
CpuSpeed (VOID)
{
  UINT32 Mux = MmioRead32 (0xffef010100ULL), Cfg0, Cfg1, Div;
  UINTN Pll = 0xffef010000ULL + ((Mux & 1) ? 0x10 : 0);
  UINT64 Rate;
  if (!(Mux & 1) && (Mux & 2))
  {
    return 24;
  }
  Cfg0 = MmioRead32 (Pll);
  Cfg1 = MmioRead32 (Pll + 4);
  if (Cfg1 & BIT30)
  {
    return 24;
  }
  Div = (Cfg0 & 63) * ((Cfg0 >> 20) & 7) * ((Cfg0 >> 24) & 7);
  if (!Div)
  {
    return 0;
  }
  Rate = LShiftU64 ((Cfg0 >> 8) & 4095, 24);
  if (!(Cfg1 & BIT24))
  {
    Rate += Cfg1 & 0xffffff;
  }
  return (UINT16)DivU64x32 (RShiftU64 (MultU64x32 (Rate, 24), 24), Div);
}
EFI_STATUS EFIAPI
SmbiosInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SMBIOS_PROTOCOL *Smbios;
  EFI_STATUS Status;
  EFI_SMBIOS_HANDLE Handle, ArrayHandle;
  SMBIOS_TABLE_TYPE0 Bios = {0};
  SMBIOS_TABLE_TYPE1 System = {0};
  SMBIOS_TABLE_TYPE4 Cpu = {0};
  SMBIOS_TABLE_TYPE16 Array = {0};
  SMBIOS_TABLE_TYPE19 Memory = {0};
  EFI_PEI_HOB_POINTERS Hob;
  UINT64 Size = 0;
  CHAR8 BiosStrings[192], SystemStrings[256];
  CONST CHAR8 *Vendor = (CONST CHAR8 *)PcdGetPtr (PcdBoardManufacturer);
  CONST CHAR8 *Firmware = (CONST CHAR8 *)PcdGetPtr (PcdFirmwareName);
  CONST CHAR8 *Product = (CONST CHAR8 *)PcdGetPtr (PcdBoardProduct);
  UINTN VendorSize = AsciiStrSize (Vendor), BiosSize, SystemSize;
  VOID *FdtHob = GetFirstGuidHob (&gFdtHobGuid);
  INT32 ModelSize;
  CONST CHAR8 *Model;
  if (FdtHob != NULL)
  {
    VOID *Fdt = (VOID *)(UINTN) * (UINT64 *)GET_GUID_HOB_DATA (FdtHob);
    Model = FdtGetProp (Fdt, 0, "model", &ModelSize);
    if (Model != NULL && ModelSize > 1 && Model[ModelSize - 1] == 0)
    {
      Product = Model;
    }
  }
  BiosSize = VendorSize + AsciiStrSize (Firmware);
  SystemSize = VendorSize + AsciiStrSize (Product);
  if (BiosSize > sizeof (BiosStrings) || SystemSize > sizeof (SystemStrings))
  {
    return EFI_BAD_BUFFER_SIZE;
  }
  CopyMem (BiosStrings, Vendor, VendorSize);
  CopyMem (BiosStrings + VendorSize, Firmware, AsciiStrSize (Firmware));
  CopyMem (SystemStrings, Vendor, VendorSize);
  CopyMem (SystemStrings + VendorSize, Product, AsciiStrSize (Product));
  STATIC CONST CHAR8 CpuStrings[] = "TH1520\0T-Head\0Xuantie C910\0";

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Bios.Hdr.Type = SMBIOS_TYPE_BIOS_INFORMATION;
  Bios.Vendor = 1;
  Bios.BiosVersion = 2;
  Bios.BiosSize = 127;
  Bios.BiosCharacteristics.BiosCharacteristicsNotSupported = 1;
  Bios.BIOSCharacteristicsExtensionBytes[1] = BIT3;
  Bios.SystemBiosMajorRelease = Bios.SystemBiosMinorRelease = 0xff;
  Bios.EmbeddedControllerFirmwareMajorRelease = Bios.EmbeddedControllerFirmwareMinorRelease = 0xff;
  Status = Add (Smbios, &Bios, sizeof (Bios), BiosStrings, BiosSize, &Handle);
  if (EFI_ERROR (Status))
  {
    return Status;
  }

  System.Hdr.Type = SMBIOS_TYPE_SYSTEM_INFORMATION;
  System.Manufacturer = 1;
  System.ProductName = 2;
  System.WakeUpType = SystemWakeupTypeUnknown;
  Status = Add (Smbios, &System, sizeof (System), SystemStrings, SystemSize, &Handle);
  if (EFI_ERROR (Status))
  {
    return Status;
  }

  Cpu.Hdr.Type = SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Cpu.Socket = 1;
  Cpu.ProcessorManufacturer = 2;
  Cpu.ProcessorVersion = 3;
  Cpu.ProcessorType = CentralProcessor;
  Cpu.ProcessorFamily = ProcessorFamilyIndicatorFamily2;
  Cpu.ProcessorFamily2 = ProcessorFamilyRiscVRV64;
  Cpu.CurrentSpeed = CpuSpeed ();
  Cpu.Status = 0x41;
  Cpu.CoreCount = Cpu.EnabledCoreCount = Cpu.ThreadCount = 4;
  Cpu.CoreCount2 = Cpu.EnabledCoreCount2 = Cpu.ThreadCount2 = Cpu.ThreadEnabled = 4;
  Cpu.L1CacheHandle = Cpu.L2CacheHandle = Cpu.L3CacheHandle = 0xffff;
  Status = Add (Smbios, &Cpu, sizeof (Cpu), CpuStrings, sizeof (CpuStrings), &Handle);
  if (EFI_ERROR (Status))
  {
    return Status;
  }

  for (Hob.Raw = GetHobList (); !END_OF_HOB_LIST (Hob); Hob.Raw = GET_NEXT_HOB (Hob))
  {
    if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR &&
        Hob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY)
    {
      Size += Hob.ResourceDescriptor->ResourceLength;
    }
  }
  if (!Size)
  {
    return EFI_NOT_FOUND;
  }
  Array.Hdr.Type = SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY;
  Array.Location = MemoryArrayLocationSystemBoard;
  Array.Use = MemoryArrayUseSystemMemory;
  Array.MemoryErrorCorrection = MemoryErrorCorrectionUnknown;
  Array.MaximumCapacity = 0x80000000;
  Array.ExtendedMaximumCapacity = Size;
  Array.MemoryErrorInformationHandle = 0xfffe;
  // LPDDR is soldered down; no DIMM sockets are exposed by this platform.
  Array.NumberOfMemoryDevices = 0;
  Status = Add (Smbios, &Array, sizeof (Array), "", 0, &ArrayHandle);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Memory.Hdr.Type = SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS;
  Memory.StartingAddress = Memory.EndingAddress = 0xffffffff;
  Memory.ExtendedEndingAddress = Size - 1;
  Memory.MemoryArrayHandle = ArrayHandle;
  return Add (Smbios, &Memory, sizeof (Memory), "", 0, &Handle);
}
