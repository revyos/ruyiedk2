// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Guid/SerialPortLibVendor.h>
#include <Guid/TtyTerm.h>
#include <Guid/EventGroup.h>
#include <Guid/PlatformHasDeviceTree.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>

#pragma pack(1)
STATIC struct {
  VENDOR_DEVICE_PATH Serial;
  UART_DEVICE_PATH Uart;
  VENDOR_DEVICE_PATH Terminal;
  EFI_DEVICE_PATH_PROTOCOL End;
} Console = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   EDKII_SERIAL_PORT_LIB_VENDOR_GUID},
  {{MESSAGING_DEVICE_PATH, MSG_UART_DP, {sizeof (UART_DEVICE_PATH), 0}},
   0, 115200, 8, 1, 1},
  {{MESSAGING_DEVICE_PATH, MSG_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   EDKII_XTERM_R6_GUID},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}
};
#pragma pack()

STATIC VOID EFIAPI Notify (EFI_EVENT Event, VOID *Context) {}

VOID EFIAPI PlatformBootManagerBeforeConsole (VOID)
{
  EFI_HANDLE Handle = NULL;
  EFI_EVENT Event;
  gBS->InstallProtocolInterface (&Handle, &gEdkiiPlatformHasDeviceTreeGuid,
                                 EFI_NATIVE_INTERFACE, NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (VOID *)&Console, NULL);
  if (!EFI_ERROR (gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                     Notify, NULL, &gEfiEndOfDxeEventGroupGuid, &Event))) {
    gBS->SignalEvent (Event);
    gBS->CloseEvent (Event);
  }
}

STATIC VOID RunFastboot (VOID)
{
  EFI_GUID Guid = {0xb7c9a614, 0x8a50, 0x4ab8,
                   {0x96, 0x22, 0xba, 0xfd, 0x03, 0x6a, 0x41, 0x8e}};
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH File;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_BOOT_MANAGER_LOAD_OPTION Option;
  if (EFI_ERROR (gBS->HandleProtocol (gImageHandle,
                   &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded))) {
    return;
  }
  EfiInitializeFwVolDevicepathNode (&File, &Guid);
  Path = AppendDevicePathNode (DevicePathFromHandle (Loaded->DeviceHandle),
                               (VOID *)&File);
  if (Path == NULL) {
    return;
  }
  if (!EFI_ERROR (EfiBootManagerInitializeLoadOption (&Option,
                   LoadOptionNumberUnassigned, LoadOptionTypeBoot,
                   LOAD_OPTION_ACTIVE, L"USB Fastboot Recovery", Path,
                   NULL, 0))) {
    EfiBootManagerBoot (&Option);
    EfiBootManagerFreeLoadOption (&Option);
  }
  FreePool (Path);
}

VOID EFIAPI PlatformBootManagerAfterConsole (VOID)
{
  EfiBootManagerConnectAll ();
  RunFastboot ();
}

VOID EFIAPI PlatformBootManagerWaitCallback (UINT16 Remaining) {}

VOID EFIAPI PlatformBootManagerUnableToBoot (VOID)
{
  for (;;) {
    RunFastboot ();
    gBS->Stall (1000000);
  }
}
