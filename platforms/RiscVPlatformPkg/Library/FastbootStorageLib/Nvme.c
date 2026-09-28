// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FastbootStorageLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PciIo.h>

STATIC INTN
CompareController (CONST FASTBOOT_NVME_TARGET *A,
                   CONST FASTBOOT_NVME_TARGET *B)
{
  if (A->Segment != B->Segment)
    return A->Segment < B->Segment ? -1 : 1;
  if (A->Bus != B->Bus)
    return A->Bus < B->Bus ? -1 : 1;
  if (A->Device != B->Device)
    return A->Device < B->Device ? -1 : 1;
  if (A->Function != B->Function)
    return A->Function < B->Function ? -1 : 1;
  return 0;
}

EFI_STATUS
FastbootLocateNvme (FASTBOOT_NVME_TARGET **Targets, UINTN *Count)
{
  EFI_HANDLE *Handles, Controller;
  UINTN HandleCount, ControllerIndex = 0;
  EFI_STATUS Status;
  FASTBOOT_NVME_TARGET *List;

  *Targets = NULL;
  *Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &HandleCount, &Handles);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR (Status))
    return Status;
  if (HandleCount > MAX_UINTN / sizeof (*List))
  {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  List = AllocateZeroPool (HandleCount * sizeof (*List));
  if (List == NULL)
  {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN I = 0; I < HandleCount; I++)
  {
    FASTBOOT_NVME_TARGET Entry = {0};
    EFI_DEVICE_PATH_PROTOCOL *Path, *Remaining;
    EFI_PCI_IO_PROTOCOL *Pci;
    UINTN Position;

    Status = gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid,
                                  (VOID **)&Entry.Io);
    if (EFI_ERROR (Status) || !Entry.Io->Media->MediaPresent
        || Entry.Io->Media->LogicalPartition
        || !Entry.Io->Media->BlockSize
        || SIZE_64KB % Entry.Io->Media->BlockSize
        || Entry.Io->Media->IoAlign > EFI_PAGE_SIZE)
      continue;
    Path = DevicePathFromHandle (Handles[I]);
    if (Path == NULL)
      continue;
    Remaining = Path;
    Status = gBS->LocateDevicePath (&gEfiPciIoProtocolGuid, &Remaining,
                                    &Controller);
    if (EFI_ERROR (Status))
      continue;
    Status = gBS->HandleProtocol (Controller, &gEfiPciIoProtocolGuid,
                                  (VOID **)&Pci);
    if (EFI_ERROR (Status))
      continue;
    Status = Pci->GetLocation (Pci, &Entry.Segment, &Entry.Bus,
                               &Entry.Device, &Entry.Function);
    if (EFI_ERROR (Status))
      continue;
    for (; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path))
      if (DevicePathType (Path) == MESSAGING_DEVICE_PATH
          && DevicePathSubType (Path) == MSG_NVME_NAMESPACE_DP
          && DevicePathNodeLength (Path)
                 == sizeof (NVME_NAMESPACE_DEVICE_PATH))
        Entry.Namespace = ReadUnaligned32 (
            &((NVME_NAMESPACE_DEVICE_PATH *)Path)->NamespaceId);
    if (!Entry.Namespace || Entry.Namespace == MAX_UINT32)
      continue;
    // PCI address orders controllers independently of handle discovery.
    Position = *Count;
    while (Position > 0)
    {
      INTN Order = CompareController (&List[Position - 1], &Entry);
      if (Order < 0 || (Order == 0
                       && List[Position - 1].Namespace <= Entry.Namespace))
        break;
      List[Position] = List[Position - 1];
      Position--;
    }
    List[Position] = Entry;
    (*Count)++;
  }
  FreePool (Handles);
  for (UINTN I = 0; I < *Count; I++)
  {
    if (I && CompareController (&List[I - 1], &List[I]))
      ControllerIndex++;
    AsciiSPrint (List[I].Name, sizeof (List[I].Name), "nvme%un%u",
                 (UINT32)ControllerIndex, List[I].Namespace);
  }
  *Targets = List;
  return EFI_SUCCESS;
}
