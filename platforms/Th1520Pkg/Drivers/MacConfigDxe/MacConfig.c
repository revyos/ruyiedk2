// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiPopup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/Th1520MacStoreLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "MacConfig.h"
#include "MacConfigDxeStrDefs.h"

STATIC EFI_GUID FormGuid = MAC_FORM_GUID;
STATIC EFI_HANDLE DriverHandle;
STATIC EFI_HII_HANDLE HiiHandle;
STATIC TH1520_MAC_CONFIG_PROTOCOL *Config;
extern UINT8 FormsBin[];
extern UINT8 MacConfigDxeStrings[];
EFI_STATUS MacParse (CONST CHAR16 *Text, UINT8 *Mac);

STATIC VOID
ShowError (EFI_STRING_ID Message)
{
  EFI_HII_POPUP_PROTOCOL *Popup;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid,
                                     NULL, (VOID **)&Popup)))
    Popup->CreatePopup (Popup, EfiHiiPopupStyleError, EfiHiiPopupTypeOk,
                        HiiHandle, Message, NULL);
}

STATIC VOID
FormatMac (CHAR16 *Text, CONST UINT8 *Mac)
{
  UnicodeSPrint (Text, 18 * sizeof (*Text),
      L"%02x:%02x:%02x:%02x:%02x:%02x",
      Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
}

STATIC EFI_STATUS
Load (MAC_FORM_DATA *Data)
{
  TH1520_MAC_SETTINGS Settings;
  TH1520_MAC_INFO Info[2];
  EFI_STATUS Status = Config->Get (&Settings, Info);
  STATIC CONST CHAR16 *Sources[] = {
    L"unavailable", L"device tree", L"BIOS setting",
    L"factory eFuse", L"automatic"
  };
  if (EFI_ERROR (Status)) return Status;
  ZeroMem (Data, sizeof (*Data));
  Data->Present0 = Info[0].Present;
  Data->Present1 = Info[1].Present;
  Data->Locked0 = Info[0].Source == Th1520MacDeviceTree;
  Data->Locked1 = Info[1].Source == Th1520MacDeviceTree;
  Data->Use0 = (Settings.OverrideMask & 1) != 0;
  Data->Use1 = (Settings.OverrideMask & 2) != 0;
  for (UINTN I = 0; I < 2; I++)
  {
    CHAR16 Current[18], Line[80];
    CHAR16 *Text = I ? Data->Address1 : Data->Address0;
    FormatMac (Text, Settings.OverrideMask & (1U << I) ?
        Settings.Address[I] : Info[I].Address);
    FormatMac (Current, Info[I].Address);
    UnicodeSPrint (Line, sizeof (Line), L"%s (%s)", Current,
                    Sources[Info[I].Source]);
    HiiSetString (HiiHandle,
        I ? STRING_TOKEN (STR_CURRENT1) : STRING_TOKEN (STR_CURRENT0),
        Line, NULL);
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Save (MAC_FORM_DATA *Data)
{
  TH1520_MAC_SETTINGS Settings, Old;
  TH1520_MAC_INFO Info[2];
  EFI_STATUS Status = Config->Get (&Old, Info);
  if (EFI_ERROR (Status)) return Status;
  ZeroMem (&Settings, sizeof (Settings));
  for (UINTN I = 0; I < 2; I++)
  {
    UINT8 Use = I ? Data->Use1 : Data->Use0;
    if (Use > 1 || (Use && !Info[I].Present))
      return EFI_INVALID_PARAMETER;
    if (Use && Info[I].Source == Th1520MacDeviceTree)
    {
      Settings.OverrideMask |= Old.OverrideMask & (1U << I);
      CopyMem (Settings.Address[I], Old.Address[I], 6);
      continue;
    }
    if (Use)
    {
      CONST CHAR16 *Text = I ? Data->Address1 : Data->Address0;
      // ConfigToBlock can supply an unterminated buffer.
      if (Text[17]) return EFI_INVALID_PARAMETER;
      Status = MacParse (Text, Settings.Address[I]);
      if (EFI_ERROR (Status)) return Status;
      Settings.OverrideMask |= 1U << I;
    }
  }
  return Config->Set (&Settings);
}

STATIC EFI_STATUS EFIAPI
Extract (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
         CONST EFI_STRING Request, EFI_STRING *Progress,
         EFI_STRING *Results)
{
  MAC_FORM_DATA Data;
  EFI_STRING Header, Full = NULL;
  EFI_STATUS Status;
  if (!Progress || !Results) return EFI_INVALID_PARAMETER;
  *Progress = Request;
  *Results = NULL;
  if (Request && !HiiIsConfigHdrMatch (Request, &FormGuid, L"MacData"))
    return EFI_NOT_FOUND;
  Status = Load (&Data);
  if (EFI_ERROR (Status)) return Status;
  if (!Request || !StrStr (Request, L"OFFSET"))
  {
    Header = HiiConstructConfigHdr (&FormGuid, L"MacData", DriverHandle);
    if (!Header) return EFI_OUT_OF_RESOURCES;
    UINTN Bytes = StrSize (Header) + 80;
    Full = AllocateZeroPool (Bytes);
    if (Full)
      UnicodeSPrint (Full, Bytes, L"%s&OFFSET=0&WIDTH=%016LX",
                      Header, (UINT64)sizeof (Data));
    FreePool (Header);
    if (!Full) return EFI_OUT_OF_RESOURCES;
  }
  Status = gHiiConfigRouting->BlockToConfig (gHiiConfigRouting,
      Full ? Full : Request, (UINT8 *)&Data, sizeof (Data),
      Results, Progress);
  if (Full)
  {
    FreePool (Full);
    *Progress = Request ? Request + StrLen (Request) : NULL;
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
Route (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
       CONST EFI_STRING Configuration, EFI_STRING *Progress)
{
  MAC_FORM_DATA Data;
  UINTN Size = sizeof (Data);
  EFI_STATUS Status;
  if (!Configuration || !Progress) return EFI_INVALID_PARAMETER;
  *Progress = Configuration;
  if (!HiiIsConfigHdrMatch (Configuration, &FormGuid, L"MacData"))
    return EFI_NOT_FOUND;
  Status = Load (&Data);
  if (EFI_ERROR (Status)) return Status;
  Status = gHiiConfigRouting->ConfigToBlock (gHiiConfigRouting,
      Configuration, (UINT8 *)&Data, &Size, Progress);
  return EFI_ERROR (Status) ? Status : Save (&Data);
}

STATIC EFI_STATUS EFIAPI
Callback (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
          EFI_BROWSER_ACTION Action, EFI_QUESTION_ID Key, UINT8 Type,
          EFI_IFR_TYPE_VALUE *Value, EFI_BROWSER_ACTION_REQUEST *Request)
{
  MAC_FORM_DATA Data;
  EFI_STATUS Status;
  if (!Request) return EFI_INVALID_PARAMETER;
  *Request = EFI_BROWSER_ACTION_REQUEST_NONE;
  if (Action == EFI_BROWSER_ACTION_CHANGING)
  {
    if (Key != KEY_MAC0 && Key != KEY_MAC1) return EFI_SUCCESS;
    if (!Value || Type != EFI_IFR_TYPE_STRING)
      return EFI_INVALID_PARAMETER;
    CHAR16 *Text = HiiGetString (HiiHandle, Value->string, NULL);
    UINT8 Mac[6];
    Status = MacParse (Text, Mac);
    if (Text) FreePool (Text);
    if (EFI_ERROR (Status)) ShowError (STRING_TOKEN (STR_INVALID));
    return Status;
  }
  if (Action != EFI_BROWSER_ACTION_CHANGED)
    return EFI_UNSUPPORTED;
  if (Key == KEY_MAC0 || Key == KEY_MAC1) return EFI_SUCCESS;
  if (!HiiGetBrowserData (&FormGuid, L"MacData", sizeof (Data),
                          (UINT8 *)&Data))
    return EFI_NOT_READY;
  if (Key == KEY_SAVE)
  {
    Status = Save (&Data);
    if (!EFI_ERROR (Status))
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    else
      ShowError (STRING_TOKEN (STR_SAVE_FAILED));
    return Status;
  }
  if (Key == KEY_GENERATE)
  {
    UINT8 Base[6], Peer[6];
    Status = Th1520MacGenerate (Base);
    if (EFI_ERROR (Status)) return Status;
    if (Data.Locked0)
    {
      TH1520_MAC_SETTINGS Settings;
      TH1520_MAC_INFO Info[2];
      Status = Config->Get (&Settings, Info);
      if (EFI_ERROR (Status)) return Status;
      CopyMem (Base, Info[0].Address, 6);
    }
    else if (Data.Present0)
    {
      Data.Use0 = TRUE;
      FormatMac (Data.Address0, Base);
    }
    if (Data.Present1 && !Data.Locked1)
    {
      Status = Th1520MacAdjacent (Base, Peer);
      if (EFI_ERROR (Status)) return Status;
      Data.Use1 = TRUE;
      FormatMac (Data.Address1, Peer);
    }
  }
  else if (Key == KEY_AUTO)
  {
    Data.Use0 = FALSE;
    Data.Use1 = FALSE;
  }
  else
    return EFI_UNSUPPORTED;
  return HiiSetBrowserData (&FormGuid, L"MacData", sizeof (Data),
      (UINT8 *)&Data, NULL) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_HII_CONFIG_ACCESS_PROTOCOL Access = {Extract, Route, Callback};
#pragma pack(1)
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} DevicePath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
    MAC_FORM_GUID},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}
};
#pragma pack()

EFI_STATUS EFIAPI
MacConfigInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  MAC_FORM_DATA Data;
  EFI_STATUS Status = gBS->LocateProtocol (&gTh1520MacConfigProtocolGuid,
                                           NULL, (VOID **)&Config);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->InstallMultipleProtocolInterfaces (&DriverHandle,
      &gEfiDevicePathProtocolGuid, &DevicePath,
      &gEfiHiiConfigAccessProtocolGuid, &Access, NULL);
  if (EFI_ERROR (Status)) return Status;
  HiiHandle = HiiAddPackages (&FormGuid, DriverHandle,
      MacConfigDxeStrings, FormsBin, NULL);
  if (!HiiHandle)
  {
    gBS->UninstallMultipleProtocolInterfaces (DriverHandle,
        &gEfiDevicePathProtocolGuid, &DevicePath,
        &gEfiHiiConfigAccessProtocolGuid, &Access, NULL);
    return EFI_OUT_OF_RESOURCES;
  }
  Load (&Data);
  return EFI_SUCCESS;
}
