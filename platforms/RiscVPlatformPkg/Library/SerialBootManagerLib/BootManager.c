// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DisplayConsoleLib.h>
#include <Library/BootProgressLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <Guid/SerialPortLibVendor.h>
#include <Guid/EventGroup.h>
#include <Guid/PlatformHasDeviceTree.h>
#include <Guid/TtyTerm.h>

#pragma pack(1)
STATIC struct
{
  VENDOR_DEVICE_PATH Serial;
  UART_DEVICE_PATH Uart;
  VENDOR_DEVICE_PATH Terminal;
  EFI_DEVICE_PATH_PROTOCOL End;
} Console = {
  { { HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH), 0 } },
    EDKII_SERIAL_PORT_LIB_VENDOR_GUID },
  { { MESSAGING_DEVICE_PATH,
      MSG_UART_DP,
      { sizeof (UART_DEVICE_PATH), 0 } },
    0,
    115200,
    8,
    1,
    1 },
  { { MESSAGING_DEVICE_PATH,
      MSG_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH), 0 } },
    EDKII_XTERM_R6_GUID },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { 4, 0 } }
};
STATIC struct
{
  USB_CLASS_DEVICE_PATH Keyboard;
  EFI_DEVICE_PATH_PROTOCOL End;
} UsbKeyboard = {
  {{MESSAGING_DEVICE_PATH, MSG_USB_CLASS_DP,
    {sizeof (USB_CLASS_DEVICE_PATH), 0}}, 0xffff, 0xffff, 3, 1, 1},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}
};
#pragma pack()
STATIC BOOLEAN
IsFirmwareApp (EFI_DEVICE_PATH_PROTOCOL *Path, CONST EFI_GUID *Guid)
{
  for (; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path))
    if (DevicePathType (Path) == MEDIA_DEVICE_PATH
        && DevicePathSubType (Path) == MEDIA_PIWG_FW_FILE_DP
        && DevicePathNodeLength (Path)
               == sizeof (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH)
        && CompareGuid (
            EfiGetNameGuidFromFwVolDevicePathNode (
                (CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Path),
            Guid))
      return TRUE;
  return FALSE;
}

STATIC BOOLEAN
RemoveDuplicateApps (EFI_GUID *Guid, CHAR16 *Name, UINTN Keep)
{
  UINTN Count;
  BOOLEAN Removed = FALSE;
  EFI_BOOT_MANAGER_LOAD_OPTION *Options
      = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (UINTN I = 0; I < Count; I++)
  {
    if (Options[I].OptionNumber == Keep || Options[I].OptionalDataSize
        || StrCmp (Options[I].Description, Name)
        || (Options[I].Attributes & LOAD_OPTION_CATEGORY)
               != LOAD_OPTION_CATEGORY_APP)
      continue;
    if (IsFirmwareApp (Options[I].FilePath, Guid)
        && !EFI_ERROR (EfiBootManagerDeleteLoadOptionVariable (
            Options[I].OptionNumber, LoadOptionTypeBoot)))
      Removed = TRUE;
  }
  EfiBootManagerFreeLoadOptions (Options, Count);
  return Removed;
}

STATIC VOID
RegisterApp (EFI_GUID *Guid, CHAR16 *Name, EFI_INPUT_KEY *Key,
             BOOLEAN Hidden)
{
  UINTN Count;
  EFI_BOOT_MANAGER_LOAD_OPTION *Existing
      = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (UINTN I = 0; I < Count; I++)
  {
    if (IsFirmwareApp (Existing[I].FilePath, Guid))
    {
      // BDS adjusts FV paths after upgrades. Retain the option number,
      // user ordering and the CRC referenced by existing hotkeys.
      BOOLEAN Removed
          = RemoveDuplicateApps (Guid, Name, Existing[I].OptionNumber);
      if (Key != NULL && Removed)
        EfiBootManagerDeleteKeyOptionVariable (NULL, 0, Key, NULL);
      if (Key != NULL)
        EfiBootManagerAddKeyOptionVariable (
            NULL, (UINT16)Existing[I].OptionNumber, 0, Key, NULL);
      EfiBootManagerFreeLoadOptions (Existing, Count);
      return;
    }
  }
  EfiBootManagerFreeLoadOptions (Existing, Count);
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH File;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_BOOT_MANAGER_LOAD_OPTION Option;
  UINT32 Attributes = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
  if (EFI_ERROR (gBS->HandleProtocol (
          gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)))
  {
    return;
  }
  EfiInitializeFwVolDevicepathNode (&File, Guid);
  Path = AppendDevicePathNode (DevicePathFromHandle (Loaded->DeviceHandle),
                               (VOID *)&File);
  if (Path == NULL)
  {
    return;
  }
  if (Hidden)
  {
    Attributes |= LOAD_OPTION_HIDDEN;
  }
  if (!EFI_ERROR (EfiBootManagerInitializeLoadOption (
          &Option, LoadOptionNumberUnassigned, LoadOptionTypeBoot,
          Attributes, Name, Path, NULL, 0)))
  {
    if (!EFI_ERROR (
            EfiBootManagerAddLoadOptionVariable (&Option, MAX_UINTN))
        && Key != NULL)
    {
      EfiBootManagerAddKeyOptionVariable (
          NULL, (UINT16)Option.OptionNumber, 0, Key, NULL);
    }
    EfiBootManagerFreeLoadOption (&Option);
  }
  FreePool (Path);
}

STATIC VOID EFIAPI
Notify (EFI_EVENT Event, VOID *Context)
{
}

VOID EFIAPI
PlatformBootManagerBeforeConsole (VOID)
{
  EFI_EVENT Event;
  EFI_BOOT_MANAGER_LOAD_OPTION Menu;
  CHAR16 *Name;
  EFI_HANDLE Handle = NULL;
  EFI_INPUT_KEY Enter = { SCAN_NULL, CHAR_CARRIAGE_RETURN };
  EFI_INPUT_KEY F2 = { SCAN_F2, 0 };
  EFI_INPUT_KEY F9 = { SCAN_F9, 0 };
  EFI_INPUT_KEY F12 = { SCAN_F12, 0 };
  EFI_GUID Shell = { 0x7c04a583,
                     0x9e3e,
                     0x4f1c,
                     { 0xad, 0x65, 0xe0, 0x52, 0x68, 0xd0, 0xb4, 0xd1 } };
  EFI_GUID Fastboot
      = { 0xb7c9a614,
          0x8a50,
          0x4ab8,
          { 0x96, 0x22, 0xba, 0xfd, 0x03, 0x6a, 0x41, 0x8e } };
  EFI_GUID BootMenu
      = { 0xeec25bdc,
          0x67f2,
          0x4d95,
          { 0xb1, 0xd5, 0xf8, 0x1b, 0x20, 0x39, 0xd1, 0x1d } };

  gBS->InstallProtocolInterface (&Handle, &gEdkiiPlatformHasDeviceTreeGuid,
                                 EFI_NATIVE_INTERFACE, NULL);
  EfiBootManagerRegisterContinueKeyOption (0, &Enter, NULL);
  if (!EFI_ERROR (EfiBootManagerGetBootManagerMenu (&Menu)))
  {
    Name
        = AllocateCopyPool (sizeof (L"Firmware Setup"), L"Firmware Setup");
    if (Name != NULL)
    {
      FreePool (Menu.Description);
      Menu.Description = Name;
      EfiBootManagerLoadOptionToVariable (&Menu);
    }
    EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)Menu.OptionNumber, 0,
                                        &F2, NULL);
    EfiBootManagerFreeLoadOption (&Menu);
  }
  RegisterApp (&BootMenu, L"Boot Menu", &F12, TRUE);
  RegisterApp (&Shell, L"UEFI Shell", NULL, FALSE);
  RegisterApp (&Fastboot, L"USB Fastboot", &F9, FALSE);
  EfiBootManagerUpdateConsoleVariable (ConIn, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn, (VOID *)&UsbKeyboard, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (VOID *)&Console, NULL);
  DisplayConsoleInitialize ();
  if (!EFI_ERROR (
          gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Notify,
                              NULL, &gEfiEndOfDxeEventGroupGuid, &Event)))
  {
    gBS->SignalEvent (Event);
    gBS->CloseEvent (Event);
  }
}

VOID EFIAPI
PlatformBootManagerAfterConsole (VOID)
{
  DisplayConsoleReady ();
  EfiBootManagerConnectAll ();
  EfiBootManagerConnectAllDefaultConsoles ();
  EfiBootManagerRefreshAllBootOption ();
  BootProgressStart ();
}

VOID EFIAPI
PlatformBootManagerWaitCallback (UINT16 Remaining)
{
  BootProgressUpdate (Remaining);
}

VOID EFIAPI
PlatformBootManagerUnableToBoot (VOID)
{
  EFI_BOOT_MANAGER_LOAD_OPTION Menu;

  if (!EFI_ERROR (EfiBootManagerGetBootManagerMenu (&Menu)))
  {
    EfiBootManagerBoot (&Menu);
    EfiBootManagerFreeLoadOption (&Menu);
  }
}
