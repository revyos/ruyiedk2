// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/FdtClient.h>
#include <Protocol/Th1520MacStore.h>
#include <Protocol/Th1520NvStore.h>
#include <Library/Th1520MacStoreLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/SerialPortLibVendor.h>
#include <Guid/EventGroup.h>
#include <Guid/PlatformHasDeviceTree.h>
#include <Guid/GlobalVariable.h>
#include <Guid/TtyTerm.h>
#include <Library/PcdLib.h>
#include <Library/BootLogoLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>

#pragma pack(1)
STATIC struct
{
  VENDOR_DEVICE_PATH Serial;
  UART_DEVICE_PATH Uart;
  VENDOR_DEVICE_PATH Terminal;
  EFI_DEVICE_PATH_PROTOCOL End;
} Console = {
    {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
     EDKII_SERIAL_PORT_LIB_VENDOR_GUID},
    {{MESSAGING_DEVICE_PATH, MSG_UART_DP, {sizeof (UART_DEVICE_PATH), 0}}, 0, 115200, 8, 1, 1},
    {{MESSAGING_DEVICE_PATH, MSG_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}}, EDKII_XTERM_R6_GUID},
    {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}};
STATIC struct
{
  USB_CLASS_DEVICE_PATH Keyboard;
  EFI_DEVICE_PATH_PROTOCOL End;
} UsbKeyboard = {{{MESSAGING_DEVICE_PATH, MSG_USB_CLASS_DP, {sizeof (USB_CLASS_DEVICE_PATH), 0}},
                  0xffff,
                  0xffff,
                  3,
                  1,
                  1},
                 {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}};
#pragma pack()
VOID EFIAPI PlatformBootManagerUnableToBoot (VOID);
STATIC VOID
RegisterApp (EFI_GUID *Guid, CHAR16 *Name, EFI_INPUT_KEY *Key, BOOLEAN Hidden);

STATIC BOOLEAN
IsFirmwareApp (EFI_DEVICE_PATH_PROTOCOL *Path, CONST EFI_GUID *Guid)
{
  for (; !IsDevicePathEnd (Path); Path = NextDevicePathNode (Path))
    if (DevicePathType (Path) == MEDIA_DEVICE_PATH &&
        DevicePathSubType (Path) == MEDIA_PIWG_FW_FILE_DP &&
        DevicePathNodeLength (Path) ==
            sizeof (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH) &&
        CompareGuid (EfiGetNameGuidFromFwVolDevicePathNode (
            (CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Path), Guid))
      return TRUE;
  return FALSE;
}

STATIC BOOLEAN
RemoveDuplicateApps (EFI_GUID *Guid, CHAR16 *Name, UINTN Keep)
{
  UINTN Count;
  BOOLEAN Removed = FALSE;
  EFI_BOOT_MANAGER_LOAD_OPTION *Options =
      EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (UINTN I = 0; I < Count; I++)
  {
    if (Options[I].OptionNumber == Keep || Options[I].OptionalDataSize ||
        StrCmp (Options[I].Description, Name) ||
        (Options[I].Attributes & LOAD_OPTION_CATEGORY) !=
            LOAD_OPTION_CATEGORY_APP)
      continue;
    if (IsFirmwareApp (Options[I].FilePath, Guid) &&
        !EFI_ERROR (EfiBootManagerDeleteLoadOptionVariable (
            Options[I].OptionNumber, LoadOptionTypeBoot)))
      Removed = TRUE;
  }
  EfiBootManagerFreeLoadOptions (Options, Count);
  return Removed;
}

STATIC VOID EFIAPI
Notify (EFI_EVENT Event, VOID *Context)
{
}
STATIC VOID *GraphicsRegistration;
STATIC BOOLEAN ConsoleReady;

STATIC VOID EFIAPI
GraphicsAvailable (EFI_EVENT Event, VOID *Context)
{
  EFI_HANDLE Handle;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINT32 Width, Height;
  UINTN Size;
  while (TRUE)
  {
    Size = sizeof (Handle);
    if (EFI_ERROR (gBS->LocateHandle (ByRegisterNotify, NULL,
        GraphicsRegistration, &Size, &Handle)))
      break;
    Path = DevicePathFromHandle (Handle);
    if (Path == NULL)
      continue;
    if (!EFI_ERROR (gBS->HandleProtocol (Handle,
        &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) &&
        Gop->Mode != NULL && Gop->Mode->Info != NULL)
    {
      // Preserve the mode selected by GOP from the sink's EDID, including
      // in setup. GraphicsConsole otherwise chooses its own resolution.
      Width = Gop->Mode->Info->HorizontalResolution;
      Height = Gop->Mode->Info->VerticalResolution;
      PcdSet32S (PcdVideoHorizontalResolution, Width);
      PcdSet32S (PcdVideoVerticalResolution, Height);
      PcdSet32S (PcdSetupVideoHorizontalResolution, Width);
      PcdSet32S (PcdSetupVideoVerticalResolution, Height);
    }
    EfiBootManagerUpdateConsoleVariable (ConOut, Path, NULL);
    EfiBootManagerUpdateConsoleVariable (ErrOut, Path, NULL);
    if (ConsoleReady)
      EfiBootManagerConnectAllDefaultConsoles ();
  }
}

VOID EFIAPI
PlatformBootManagerBeforeConsole (VOID)
{
  EFI_EVENT Event;
  EFI_BOOT_MANAGER_LOAD_OPTION Menu;
  CHAR16 *SetupName;
  EFI_INPUT_KEY Esc = {SCAN_ESC, 0};
  EFI_INPUT_KEY F2 = {SCAN_F2, 0}, F9 = {SCAN_F9, 0}, F12 = {SCAN_F12, 0};
  EFI_GUID Shell = {0x7c04a583, 0x9e3e, 0x4f1c, {0xad, 0x65, 0xe0, 0x52, 0x68, 0xd0, 0xb4, 0xd1}};
  EFI_GUID Fastboot = {
      0xb7c9a614, 0x8a50, 0x4ab8, {0x96, 0x22, 0xba, 0xfd, 0x03, 0x6a, 0x41, 0x8e}};
  EFI_GUID BootMenu = {
      0xeec25bdc, 0x67f2, 0x4d95, {0xb1, 0xd5, 0xf8, 0x1b, 0x20, 0x39, 0xd1, 0x1d}};
  EFI_HANDLE Handle = NULL;
  gBS->InstallProtocolInterface (&Handle, &gEdkiiPlatformHasDeviceTreeGuid, EFI_NATIVE_INTERFACE,
                                 NULL);
  if (!EFI_ERROR (EfiBootManagerGetBootManagerMenu (&Menu)))
  {
    SetupName = AllocateCopyPool (sizeof (L"Firmware Setup"), L"Firmware Setup");
    if (SetupName != NULL)
    {
      FreePool (Menu.Description);
      Menu.Description = SetupName;
      // Set the user-facing name before hotkeys record the option's CRC.
      EfiBootManagerLoadOptionToVariable (&Menu);
    }
    if (RemoveDuplicateApps (PcdGetPtr (PcdBootManagerMenuFile),
                              L"Firmware Setup", Menu.OptionNumber))
    {
      EfiBootManagerDeleteKeyOptionVariable (NULL, 0, &Esc, NULL);
      EfiBootManagerDeleteKeyOptionVariable (NULL, 0, &F2, NULL);
    }
    EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)Menu.OptionNumber, 0, &Esc, NULL);
    EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)Menu.OptionNumber, 0, &F2, NULL);
    EfiBootManagerFreeLoadOption (&Menu);
  }
  RegisterApp (&BootMenu, L"Boot Menu", &F12, TRUE);
  // BDS starts its hotkey service immediately after this callback. Register
  // F9 alongside F2/F12 so console connection cannot consume it too early.
  RegisterApp (&Shell, L"UEFI Shell", NULL, FALSE);
  RegisterApp (&Fastboot, L"USB Fastboot", &F9, FALSE);
  EfiBootManagerUpdateConsoleVariable (ConIn, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn, (VOID *)&UsbKeyboard, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (VOID *)&Console, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (VOID *)&Console, NULL);
  EfiCreateProtocolNotifyEvent (&gEfiGraphicsOutputProtocolGuid,
      TPL_CALLBACK, GraphicsAvailable, NULL, &GraphicsRegistration);
  if (!EFI_ERROR (gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Notify, NULL,
                                      &gEfiEndOfDxeEventGroupGuid, &Event)))
  {
    gBS->SignalEvent (Event);
    gBS->CloseEvent (Event);
  }
}
STATIC VOID
RegisterApp (EFI_GUID *Guid, CHAR16 *Name, EFI_INPUT_KEY *Key, BOOLEAN Hidden)
{
  UINTN Count;
  EFI_BOOT_MANAGER_LOAD_OPTION *Existing =
      EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
  for (UINTN I = 0; I < Count; I++)
  {
    if (IsFirmwareApp (Existing[I].FilePath, Guid))
    {
      // BDS adjusts FV paths after upgrades. Retain the option number,
      // user ordering and the CRC referenced by existing hotkeys.
      BOOLEAN Removed = RemoveDuplicateApps (
          Guid, Name, Existing[I].OptionNumber);
      if (Key != NULL && Removed)
        EfiBootManagerDeleteKeyOptionVariable (NULL, 0, Key, NULL);
      if (Key != NULL)
        EfiBootManagerAddKeyOptionVariable (NULL,
            (UINT16)Existing[I].OptionNumber, 0, Key, NULL);
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
  if (EFI_ERROR (
          gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)))
  {
    return;
  }
  EfiInitializeFwVolDevicepathNode (&File, Guid);
  Path = AppendDevicePathNode (DevicePathFromHandle (Loaded->DeviceHandle), (VOID *)&File);
  if (Path == NULL)
  {
    return;
  }
  if (Hidden)
  {
    Attributes |= LOAD_OPTION_HIDDEN;
  }
  if (!EFI_ERROR (EfiBootManagerInitializeLoadOption (&Option, LoadOptionNumberUnassigned,
                                                      LoadOptionTypeBoot, Attributes, Name, Path,
                                                      NULL, 0)))
  {
    if (!EFI_ERROR (EfiBootManagerAddLoadOptionVariable (&Option, MAX_UINTN)) && Key != NULL)
    {
      EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)Option.OptionNumber, 0, Key, NULL);
    }
    EfiBootManagerFreeLoadOption (&Option);
  }
  FreePool (Path);
}
STATIC BOOLEAN
AdjacentToGmac0 (FDT_CLIENT_PROTOCOL *Fdt, CONST UINT8 *Mac)
{
  INT32 Node = -1;
  CONST VOID *P;
  UINT32 Size;
  UINT8 Adjacent[6];
  while (!EFI_ERROR (Fdt->FindNextCompatibleNode (
      Fdt, "thead,th1520-gmac", Node, &Node)))
    if (!EFI_ERROR (Fdt->GetNodeProperty (Fdt, Node, "reg", &P, &Size)) &&
        Size == 32 &&
        SwapBytes64 (ReadUnaligned64 (P)) == 0xffe7070000ULL &&
        !EFI_ERROR (Fdt->GetNodeProperty (
            Fdt, Node, "mac-address", &P, &Size)) && Size == 6 &&
        !EFI_ERROR (Th1520MacAdjacent (P, Adjacent)))
      return !CompareMem (Adjacent, Mac, 6);
  return FALSE;
}

STATIC VOID
ReportNetworkAddress (VOID)
{
  FDT_CLIENT_PROTOCOL *Fdt;
  CONST VOID *Reason, *Address;
  CONST UINT8 *Mac;
  CONST CHAR16 *Description;
  UINT32 Size;
  INT32 Node = -1;
  if (EFI_ERROR (gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&Fdt)))
  {
    return;
  }
  while (!EFI_ERROR (Fdt->FindNextCompatibleNode (Fdt, "thead,th1520-gmac", Node, &Node)))
  {
    if (EFI_ERROR (Fdt->GetNodeProperty (Fdt, Node, "thead,random-mac-reason", &Reason, &Size)))
    {
      continue;
    }
    Description = L"unavailable";
    if (Size == sizeof ("empty") && CompareMem (Reason, "empty", Size) == 0)
    {
      Description = L"empty";
    }
    else if (Size == sizeof ("invalid") && CompareMem (Reason, "invalid", Size) == 0)
    {
      Description = L"invalid";
    }
    else if (Size == sizeof ("duplicate") && CompareMem (Reason, "duplicate", Size) == 0)
    {
      Description = L"duplicated";
    }
    if (EFI_ERROR (Fdt->GetNodeProperty (Fdt, Node, "local-mac-address", &Address, &Size)) ||
        Size != 6)
    {
      continue;
    }
    UINT8 Saved[6];
    UINTN SavedSize = sizeof (Saved);
    UINT32 Attributes = 0;
    Mac = Address;
    EFI_STATUS Status;
    TH1520_MAC_STORE_PROTOCOL *Store;
    if (PcdGetBool (PcdFirmwareOnEmmc))
    {
      Status = gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                     NULL, (VOID **)&Store);
      if (!EFI_ERROR (Status))
        Status = Store->Read (Saved);
      Attributes = EFI_VARIABLE_NON_VOLATILE;
    }
    else
    {
      Status = gRT->GetVariable (L"FallbackMac", &gTh1520NvStoreGuid,
                                 &Attributes, &SavedSize, Saved);
    }
    BOOLEAN Persistent = !EFI_ERROR (Status) && SavedSize == 6 &&
        (Attributes & EFI_VARIABLE_NON_VOLATILE) &&
        !CompareMem (Saved, Mac, 5) &&
        (Mac[5] == Saved[5] || Mac[5] == (UINT8)(Saved[5] + 1));
    Print (L"Warning: Factory Ethernet MAC is %s.\r\n", Description);
    BOOLEAN Derived = AdjacentToGmac0 (Fdt, Mac);
    Print (L"Using %s MAC %02x:%02x:%02x:%02x:%02x:%02x.\r\n",
        Derived ? L"GMAC0-adjacent" :
        Persistent ? L"saved local" : L"temporary random", Mac[0],
        Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    if (!Persistent && !Derived)
      Print (L"MAC could not be saved; it may change after reset.\r\n");
  }
}
STATIC UINT16 mBootTimeout;
STATIC UINTN mProgress;
STATIC BOOLEAN mWaiting, mGraphicsProgress;

STATIC VOID EFIAPI
FinishStartup (EFI_EVENT Event, VOID *Context)
{
  if (mWaiting)
  {
    SerialPortWrite ((UINT8 *)"\r\n", 2);
    mWaiting = FALSE;
    // Include USB devices which appeared during the native BDS hotkey wait.
    EfiBootManagerRefreshAllBootOption ();
  }
  BootLogoDisableLogo ();
}

VOID EFIAPI
PlatformBootManagerAfterConsole (VOID)
{
  EFI_EVENT Event;
  STATIC CONST CHAR8 Keys[] = "F2: Setup    F9: USB Fastboot    F12: Boot Menu\r\n";
  ConsoleReady = TRUE;
  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();
  ReportNetworkAddress ();
  mBootTimeout = PcdGet16 (PcdPlatformBootTimeOut);
  mProgress = 0;
  mWaiting = mBootTimeout != 0;
  mGraphicsProgress = !EFI_ERROR (BootLogoEnableLogo ());
  SerialPortWrite ((UINT8 *)Keys, sizeof (Keys) - 1);
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, FinishStartup, NULL,
                      &gEfiEventReadyToBootGuid, &Event);
  // BDS owns the single timeout and invokes the registered hotkey options.
}

VOID EFIAPI
PlatformBootManagerWaitCallback (UINT16 Remaining)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White = {0xff, 0xff, 0xff, 0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = {0, 0, 0, 0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Blue = {0xdd, 0x63, 0x3e, 0};
  CHAR16 Title[80];
  CHAR8 Serial[64];
  UINTN Progress, Size, SecondsWidth = 1;
  if (mBootTimeout == 0)
  {
    return;
  }
  Progress = (mBootTimeout - MIN (Remaining, mBootTimeout)) * 100U / mBootTimeout;
  // BootLogoUpdateProgress centers each title without erasing the old one.
  // Keep its width fixed when the countdown loses a decimal digit.
  for (UINTN Timeout = mBootTimeout; Timeout >= 10; Timeout /= 10)
    SecondsWidth++;
  UnicodeSPrint (Title, sizeof (Title),
      L"F2: Setup  F9: Fastboot  F12: Boot Menu  (%*us)",
      SecondsWidth, Remaining);
  if (mGraphicsProgress)
  {
    BootLogoUpdateProgress (White, Black, Title, Blue, Progress, mProgress);
  }
  mProgress = Progress;
  Size = AsciiSPrint (Serial, sizeof (Serial), "\rBooting in %2u seconds...", Remaining);
  SerialPortWrite ((UINT8 *)Serial, Size);
  if (Remaining == 0)
  {
    FinishStartup (NULL, NULL);
  }
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
