// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/A210BootMediaLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/NonDiscoverableDevice.h>

EFI_STATUS
A210LocateBootAreas (EFI_BLOCK_IO_PROTOCOL *Boot[2])
{
  EFI_HANDLE *Handles, Controller;
  UINTN Count;
  EFI_STATUS Status;

  ZeroMem (Boot, 2 * sizeof (*Boot));
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR (Status))
    return Status;
  for (UINTN I = 0; I < Count; I++)
  {
    EFI_BLOCK_IO_PROTOCOL *Io;
    NON_DISCOVERABLE_DEVICE *Device;
    EFI_DEVICE_PATH_PROTOCOL *Path, *Remaining;
    UINTN Partition = MAX_UINTN;
    BOOLEAN Emmc = FALSE;

    Status = gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid,
                                  (VOID **)&Io);
    if (EFI_ERROR (Status) || !Io->Media->MediaPresent
        || Io->Media->LogicalPartition || Io->Media->BlockSize != 512
        || Io->Media->IoAlign > EFI_PAGE_SIZE)
      continue;
    Path = DevicePathFromHandle (Handles[I]);
    if (Path == NULL)
      continue;
    Remaining = Path;
    Status = gBS->LocateDevicePath (
        &gEdkiiNonDiscoverableDeviceProtocolGuid, &Remaining, &Controller);
    if (EFI_ERROR (Status))
      continue;
    Status = gBS->HandleProtocol (Controller,
                                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                                  (VOID **)&Device);
    if (EFI_ERROR (Status) || Device->Resources == NULL
        || Device->Resources->AddrRangeMin != A210_EMMC_BASE)
      continue;
    for (; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path))
    {
      if (DevicePathType (Path) == MESSAGING_DEVICE_PATH
          && DevicePathSubType (Path) == MSG_EMMC_DP
          && DevicePathNodeLength (Path) == sizeof (EMMC_DEVICE_PATH))
        Emmc = TRUE;
      if (DevicePathType (Path) == HARDWARE_DEVICE_PATH
          && DevicePathSubType (Path) == HW_CONTROLLER_DP
          && DevicePathNodeLength (Path)
                 == sizeof (CONTROLLER_DEVICE_PATH))
        Partition = ReadUnaligned32 (
            &((CONTROLLER_DEVICE_PATH *)Path)->ControllerNumber);
    }
    if (Emmc && Partition >= 1 && Partition <= 2)
      Boot[Partition - 1] = Io;
  }
  FreePool (Handles);
  return EFI_SUCCESS;
}
