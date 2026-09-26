// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Guid/FdtHob.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformSmbiosLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Smbios.h>

STATIC EFI_STATUS
Add (EFI_SMBIOS_PROTOCOL *Smbios, CONST VOID *Record, UINTN Size,
     CONST CHAR8 **Strings, UINTN Count, EFI_SMBIOS_HANDLE *Handle)
{
  UINTN Bytes = Size + 2;
  EFI_STATUS Status;
  UINT8 *Buffer, *Next;

  for (UINTN I = 0; I < Count; I++)
    Bytes += AsciiStrSize (Strings[I]);
  Buffer = AllocateZeroPool (Bytes);
  if (Buffer == NULL)
    return EFI_OUT_OF_RESOURCES;
  CopyMem (Buffer, Record, Size);
  ((EFI_SMBIOS_TABLE_HEADER *)Buffer)->Length = (UINT8)Size;
  Next = Buffer + Size;
  for (UINTN I = 0; I < Count; I++)
  {
    UINTN Length = AsciiStrSize (Strings[I]);
    CopyMem (Next, Strings[I], Length);
    Next += Length;
  }
  *Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->Add (Smbios, NULL, Handle, (VOID *)Buffer);
  FreePool (Buffer);
  return Status;
}

STATIC EFI_STATUS
AddProcessor (EFI_SMBIOS_PROTOCOL *Smbios, CONST VOID *Fdt)
{
  PLATFORM_PROCESSOR_INFO Info;
  SMBIOS_TABLE_TYPE4 Cpu = { 0 };
  EFI_SMBIOS_HANDLE Handle;
  EFI_STATUS Status = PlatformGetProcessorInfo (Fdt, &Info);
  CONST CHAR8 *Strings[] = { "SoC", Info.Manufacturer, Info.Name };

  if (EFI_ERROR (Status))
    return Status;
  Cpu.Hdr.Type = SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Cpu.Socket = 1;
  Cpu.ProcessorManufacturer = 2;
  Cpu.ProcessorVersion = 3;
  Cpu.ProcessorType = CentralProcessor;
  Cpu.ProcessorFamily = ProcessorFamilyIndicatorFamily2;
  Cpu.ProcessorFamily2 = ProcessorFamilyRiscVRV64;
  Cpu.CurrentSpeed = Info.CurrentMhz;
  Cpu.Status = 0x41; // Socket populated and processor enabled.
  Cpu.CoreCount = Cpu.ThreadCount = (UINT8)MIN (Info.Cores, MAX_UINT8);
  Cpu.EnabledCoreCount = (UINT8)MIN (Info.EnabledCores, MAX_UINT8);
  Cpu.CoreCount2 = Cpu.ThreadCount2 = Info.Cores;
  Cpu.EnabledCoreCount2 = Cpu.ThreadEnabled = Info.EnabledCores;
  Cpu.L1CacheHandle = Cpu.L2CacheHandle = Cpu.L3CacheHandle = 0xffff;
  return Add (Smbios, &Cpu, sizeof (Cpu), Strings, ARRAY_SIZE (Strings),
              &Handle);
}

STATIC EFI_STATUS
AddMemory (EFI_SMBIOS_PROTOCOL *Smbios)
{
  EFI_PEI_HOB_POINTERS Hob;
  SMBIOS_TABLE_TYPE16 Array = { 0 };
  EFI_SMBIOS_HANDLE ArrayHandle, Handle;
  EFI_STATUS Status;
  UINT64 Size = 0;

  // SEC derives these resource ranges from the runtime FDT supplied
  // after DDR training. Reservations do not reduce installed capacity.
  for (Hob.Raw = GetHobList (); !END_OF_HOB_LIST (Hob);
       Hob.Raw = GET_NEXT_HOB (Hob))
    if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR
        && Hob.ResourceDescriptor->ResourceType
               == EFI_RESOURCE_SYSTEM_MEMORY)
      Size += Hob.ResourceDescriptor->ResourceLength;
  if (Size == 0)
    return EFI_NOT_FOUND;
  Array.Hdr.Type = SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY;
  Array.Location = MemoryArrayLocationSystemBoard;
  Array.Use = MemoryArrayUseSystemMemory;
  Array.MemoryErrorCorrection = MemoryErrorCorrectionUnknown;
  Array.MaximumCapacity = 0x80000000;
  Array.ExtendedMaximumCapacity = Size;
  Array.MemoryErrorInformationHandle = 0xfffe;
  // Package inventory is not described by the FDT; do not invent DIMMs.
  Status = Add (Smbios, &Array, sizeof (Array), NULL, 0, &ArrayHandle);
  if (EFI_ERROR (Status))
    return Status;
  for (Hob.Raw = GetHobList (); !END_OF_HOB_LIST (Hob);
       Hob.Raw = GET_NEXT_HOB (Hob))
  {
    if (GET_HOB_TYPE (Hob) != EFI_HOB_TYPE_RESOURCE_DESCRIPTOR
        || Hob.ResourceDescriptor->ResourceType
               != EFI_RESOURCE_SYSTEM_MEMORY
        || Hob.ResourceDescriptor->ResourceLength == 0)
      continue;
    SMBIOS_TABLE_TYPE19 Memory = { 0 };
    Memory.Hdr.Type = SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS;
    Memory.StartingAddress = Memory.EndingAddress = MAX_UINT32;
    Memory.ExtendedStartingAddress = Hob.ResourceDescriptor->PhysicalStart;
    Memory.ExtendedEndingAddress = Memory.ExtendedStartingAddress
                                   + Hob.ResourceDescriptor->ResourceLength
                                   - 1;
    Memory.MemoryArrayHandle = ArrayHandle;
    Memory.PartitionWidth = 0xff; // Unknown physical interleave width.
    Status = Add (Smbios, &Memory, sizeof (Memory), NULL, 0, &Handle);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
SmbiosInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SMBIOS_PROTOCOL *Smbios;
  SMBIOS_TABLE_TYPE0 Bios = { 0 };
  SMBIOS_TABLE_TYPE1 System = { 0 };
  EFI_SMBIOS_HANDLE Handle;
  EFI_STATUS Status;
  VOID *Hob, *Fdt;
  CHAR8 Vendor[128], Version[128];
  CONST CHAR8 *Model;
  INT32 Length;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL,
                                (VOID **)&Smbios);
  if (EFI_ERROR (Status))
    return Status;
  Hob = GetFirstGuidHob (&gFdtHobGuid);
  if (Hob == NULL || GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (UINT64))
    return EFI_NOT_FOUND;
  Fdt = (VOID *)(UINTN)ReadUnaligned64 (GET_GUID_HOB_DATA (Hob));
  if (FdtCheckHeader (Fdt))
    return EFI_COMPROMISED_DATA;
  Model = FdtGetProp (Fdt, 0, "model", &Length);
  if (Model == NULL || Length <= 1 || Length > 256
      || AsciiStrnLenS (Model, Length) != (UINTN)Length - 1)
    return EFI_COMPROMISED_DATA;
  Status = UnicodeStrToAsciiStrS (PcdGetPtr (PcdFirmwareVendor), Vendor,
                                  sizeof (Vendor));
  if (EFI_ERROR (Status))
    return Status;
  Status = UnicodeStrToAsciiStrS (PcdGetPtr (PcdFirmwareVersionString),
                                  Version, sizeof (Version));
  if (EFI_ERROR (Status))
    return Status;
  CONST CHAR8 *BiosStrings[] = { Vendor, Version };
  CONST CHAR8 *SystemStrings[] = { Vendor, Model };
  Bios.Hdr.Type = SMBIOS_TYPE_BIOS_INFORMATION;
  Bios.Vendor = 1;
  Bios.BiosVersion = 2;
  UINT32 FdSize = PcdGet32 (PcdOvmfFirmwareFdSize);
  Bios.BiosSize
      = FdSize >= SIZE_16MB ? MAX_UINT8 : (UINT8)(FdSize / SIZE_64KB - 1);
  Bios.ExtendedBiosSize.Size = (UINT16)(FdSize / SIZE_1MB);
  Bios.BiosCharacteristics.BiosCharacteristicsNotSupported = 1;
  Bios.BIOSCharacteristicsExtensionBytes[1] = BIT3;
  Bios.SystemBiosMajorRelease = Bios.SystemBiosMinorRelease = 0xff;
  Bios.EmbeddedControllerFirmwareMajorRelease = 0xff;
  Bios.EmbeddedControllerFirmwareMinorRelease = 0xff;
  Status = Add (Smbios, &Bios, sizeof (Bios), BiosStrings,
                ARRAY_SIZE (BiosStrings), &Handle);
  if (EFI_ERROR (Status))
    return Status;
  System.Hdr.Type = SMBIOS_TYPE_SYSTEM_INFORMATION;
  System.Manufacturer = 1;
  System.ProductName = 2;
  System.WakeUpType = SystemWakeupTypeUnknown;
  Status = Add (Smbios, &System, sizeof (System), SystemStrings,
                ARRAY_SIZE (SystemStrings), &Handle);
  if (EFI_ERROR (Status))
    return Status;
  Status = AddProcessor (Smbios, Fdt);
  if (EFI_ERROR (Status))
    DEBUG ((DEBUG_WARN, "SMBIOS processor information: %r\n", Status));
  return AddMemory (Smbios);
}
