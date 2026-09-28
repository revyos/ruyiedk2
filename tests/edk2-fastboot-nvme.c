// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platforms/RiscVPlatformPkg/Library/FastbootStorageLib/Nvme.c"

EFI_GUID gEfiBlockIoProtocolGuid, gEfiPciIoProtocolGuid;
EFI_GUID gEfiDevicePathProtocolGuid;
typedef struct
{
  EFI_PCI_IO_PROTOCOL Pci;
  EFI_BLOCK_IO_PROTOCOL Io;
  EFI_BLOCK_IO_MEDIA Media;
  struct
  {
    NVME_NAMESPACE_DEVICE_PATH Nvme;
    EFI_DEVICE_PATH_PROTOCOL End;
  } Path;
  UINTN Bus;
} DEVICE;
static DEVICE Devices[6];
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P)
{ UINT32 V; memcpy (&V, P, 4); return V; }
VOID *EFIAPI AllocateZeroPool (UINTN N) { return calloc (1, N); }
VOID EFIAPI FreePool (VOID *P) { free (P); }
UINTN EFIAPI AsciiSPrint (CHAR8 *B, UINTN N, CONST CHAR8 *Format, ...)
{
  va_list Ap;
  va_start (Ap, Format);
  int Size = vsnprintf (B, N, Format, Ap);
  va_end (Ap);
  return Size;
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI DevicePathFromHandle (EFI_HANDLE H)
{ return &((DEVICE *)H)->Path.Nvme.Header; }
UINT8 EFIAPI DevicePathType (CONST VOID *P)
{ return ((CONST EFI_DEVICE_PATH_PROTOCOL *)P)->Type; }
UINT8 EFIAPI DevicePathSubType (CONST VOID *P)
{ return ((CONST EFI_DEVICE_PATH_PROTOCOL *)P)->SubType; }
UINTN EFIAPI DevicePathNodeLength (CONST VOID *P)
{
  CONST EFI_DEVICE_PATH_PROTOCOL *D = P;
  return D->Length[0] | (D->Length[1] << 8);
}
BOOLEAN EFIAPI IsDevicePathEnd (CONST VOID *P)
{ return DevicePathType (P) == END_DEVICE_PATH_TYPE; }
EFI_DEVICE_PATH_PROTOCOL *EFIAPI NextDevicePathNode (CONST VOID *P)
{ return (VOID *)((UINT8 *)P + DevicePathNodeLength (P)); }
static EFI_STATUS EFIAPI
Handles (EFI_LOCATE_SEARCH_TYPE Type, EFI_GUID *Guid, VOID *Key,
         UINTN *Count, EFI_HANDLE **Buffer)
{
  (void)Type; (void)Guid; (void)Key;
  *Count = 6;
  *Buffer = calloc (*Count, sizeof (**Buffer));
  for (UINTN I = 0; I < *Count; I++) (*Buffer)[I] = &Devices[I];
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Protocol (EFI_HANDLE H, EFI_GUID *Guid, VOID **Out)
{
  DEVICE *D = H;
  *Out = Guid == &gEfiBlockIoProtocolGuid ? (VOID *)&D->Io
                                         : (VOID *)&D->Pci;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Locate (EFI_GUID *Guid, EFI_DEVICE_PATH_PROTOCOL **Path, EFI_HANDLE *H)
{
  (void)Guid;
  for (UINTN I = 0; I < 6; I++)
    if (*Path == &Devices[I].Path.Nvme.Header)
    {
      *H = &Devices[I];
      return EFI_SUCCESS;
    }
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI
Location (EFI_PCI_IO_PROTOCOL *P, UINTN *Seg, UINTN *Bus,
          UINTN *Dev, UINTN *Func)
{
  *Seg = *Dev = *Func = 0;
  *Bus = ((DEVICE *)P)->Bus;
  return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES Services = {
  .LocateHandleBuffer = Handles, .HandleProtocol = Protocol,
  .LocateDevicePath = Locate
};
EFI_BOOT_SERVICES *gBS = &Services;
int main (void)
{
  FASTBOOT_NVME_TARGET *List;
  UINTN Count;
  for (UINTN I = 0; I < 6; I++)
  {
    DEVICE *D = &Devices[I];
    D->Pci.GetLocation = Location;
    D->Bus = 8;
    D->Io.Media = &D->Media;
    D->Media.MediaPresent = TRUE;
    D->Media.BlockSize = 512;
    D->Path.Nvme.Header.Type = MESSAGING_DEVICE_PATH;
    D->Path.Nvme.Header.SubType = MSG_NVME_NAMESPACE_DP;
    D->Path.Nvme.Header.Length[0] = sizeof (NVME_NAMESPACE_DEVICE_PATH);
    D->Path.Nvme.NamespaceId = I + 1;
    D->Path.End.Type = END_DEVICE_PATH_TYPE;
    D->Path.End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
    D->Path.End.Length[0] = sizeof (EFI_DEVICE_PATH_PROTOCOL);
  }
  Devices[0].Path.Nvme.NamespaceId = 3;
  Devices[1].Bus = 4;
  Devices[1].Path.Nvme.NamespaceId = 1;
  Devices[2].Path.Nvme.NamespaceId = 1;
  Devices[3].Media.LogicalPartition = TRUE;
  Devices[4].Media.MediaPresent = FALSE;
  Devices[5].Path.Nvme.Header.SubType = MSG_SATA_DP;
  assert (FastbootLocateNvme (&List, &Count) == EFI_SUCCESS && Count == 3);
  assert (!strcmp (List[0].Name, "nvme0n1")
          && List[0].Io == &Devices[1].Io);
  assert (!strcmp (List[1].Name, "nvme1n1")
          && List[1].Io == &Devices[2].Io);
  assert (!strcmp (List[2].Name, "nvme1n3")
          && List[2].Io == &Devices[0].Io);
  FreePool (List);
  puts ("NVMe ordering, namespace IDs and whole-disk filters passed");
  return 0;
}
