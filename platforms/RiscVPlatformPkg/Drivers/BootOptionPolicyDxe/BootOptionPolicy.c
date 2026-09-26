// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PlatformBootManager.h>

STATIC BOOLEAN
IsEmmcBootArea (CONST EFI_DEVICE_PATH_PROTOCOL *Path)
{
  for (; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path))
  {
    if (DevicePathType (Path) != MESSAGING_DEVICE_PATH
        || DevicePathSubType (Path) != MSG_EMMC_DP
        || DevicePathNodeLength (Path) != sizeof (EMMC_DEVICE_PATH))
      continue;
    CONST EFI_DEVICE_PATH_PROTOCOL *Area = NextDevicePathNode (Path);
    if (DevicePathType (Area) == HARDWARE_DEVICE_PATH
        && DevicePathSubType (Area) == HW_CONTROLLER_DP
        && DevicePathNodeLength (Area) == sizeof (CONTROLLER_DEVICE_PATH))
    {
      UINT32 Number = ReadUnaligned32 (
          &((CONST CONTROLLER_DEVICE_PATH *)Area)->ControllerNumber);
      return Number == 1 || Number == 2;
    }
  }
  return FALSE;
}

STATIC EFI_STATUS EFIAPI
Refresh (CONST EFI_BOOT_MANAGER_LOAD_OPTION *Options, CONST UINTN Count,
         EFI_BOOT_MANAGER_LOAD_OPTION **Updated, UINTN *UpdatedCount)
{
  EFI_BOOT_MANAGER_LOAD_OPTION *Result;
  EFI_STATUS Status;
  UINTN Keep = 0, Copied = 0;

  if (Updated == NULL || UpdatedCount == NULL
      || (Options == NULL && Count != 0))
    return EFI_INVALID_PARAMETER;
  *Updated = NULL;
  *UpdatedCount = 0;
  for (UINTN I = 0; I < Count; I++)
    if (!IsEmmcBootArea (Options[I].FilePath))
      Keep++;
  if (Keep == Count)
    return EFI_UNSUPPORTED;
  if (Keep == 0)
    return EFI_SUCCESS;
  Result = AllocateZeroPool (Keep * sizeof (*Result));
  if (Result == NULL)
    return EFI_OUT_OF_RESOURCES;
  for (UINTN I = 0; I < Count; I++)
  {
    if (IsEmmcBootArea (Options[I].FilePath))
      continue;
    Status = EfiBootManagerInitializeLoadOption (
        &Result[Copied], Options[I].OptionNumber, Options[I].OptionType,
        Options[I].Attributes, Options[I].Description, Options[I].FilePath,
        Options[I].OptionalData, Options[I].OptionalDataSize);
    if (EFI_ERROR (Status))
    {
      EfiBootManagerFreeLoadOptions (Result, Copied);
      return Status;
    }
    Copied++;
  }
  *Updated = Result;
  *UpdatedCount = Copied;
  return EFI_SUCCESS;
}

STATIC EDKII_PLATFORM_BOOT_MANAGER_PROTOCOL mPolicy
    = { EDKII_PLATFORM_BOOT_MANAGER_PROTOCOL_REVISION, Refresh };

EFI_STATUS EFIAPI
BootOptionPolicyInitialize (EFI_HANDLE Image,
                            EFI_SYSTEM_TABLE *SystemTable)
{
  return gBS->InstallProtocolInterface (
      &Image, &gEdkiiPlatformBootManagerProtocolGuid, EFI_NATIVE_INTERFACE,
      &mPolicy);
}
