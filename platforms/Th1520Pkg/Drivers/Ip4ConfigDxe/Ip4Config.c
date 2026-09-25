// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiPopup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Library/UefiLib.h>
#include "Ip4Config.h"
#include "Ip4ConfigDxeStrDefs.h"

typedef struct {
  EFI_HII_CONFIG_ACCESS_PROTOCOL Access;
  EFI_IP4_CONFIG2_PROTOCOL *Config;
  EFI_HANDLE Controller;
  EFI_HII_HANDLE Hii;
} IP4_SETUP;

STATIC EFI_GUID FormGuid = IP4_FORM_GUID;
extern UINT8 FormsBin[];
extern UINT8 Ip4ConfigDxeStrings[];

STATIC EFI_STATUS EFIAPI
Extract (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
         CONST EFI_STRING Request, EFI_STRING *Progress,
         EFI_STRING *Results)
{
  IP4_SETUP *Setup = (IP4_SETUP *)This;
  IP4_FORM_DATA Data;
  EFI_STRING Header, Full = NULL;
  EFI_STATUS Status;
  if (!Progress || !Results) return EFI_INVALID_PARAMETER;
  *Progress = Request;
  *Results = NULL;
  if (Request && !HiiIsConfigHdrMatch (Request, &FormGuid, L"Ip4Data"))
    return EFI_NOT_FOUND;
  Status = Ip4Load (Setup->Config, &Data);
  if (EFI_ERROR (Status)) return Status;
  if (!Request || !StrStr (Request, L"OFFSET")) {
    Header = HiiConstructConfigHdr (&FormGuid, L"Ip4Data",
                                     Setup->Controller);
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
  if (Full) {
    FreePool (Full);
    *Progress = Request ? Request + StrLen (Request) : NULL;
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
Route (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
       CONST EFI_STRING Configuration, EFI_STRING *Progress)
{
  IP4_SETUP *Setup = (IP4_SETUP *)This;
  IP4_FORM_DATA Data;
  UINTN Size = sizeof (Data);
  EFI_STATUS Status;
  if (!Configuration || !Progress) return EFI_INVALID_PARAMETER;
  *Progress = Configuration;
  if (!HiiIsConfigHdrMatch (Configuration, &FormGuid, L"Ip4Data"))
    return EFI_NOT_FOUND;
  // Preserve fields omitted by a partial configuration request.
  Status = Ip4Load (Setup->Config, &Data);
  if (EFI_ERROR (Status)) return Status;
  Status = gHiiConfigRouting->ConfigToBlock (gHiiConfigRouting,
      Configuration, (UINT8 *)&Data, &Size, Progress);
  if (!EFI_ERROR (Status)) Status = Ip4Save (Setup->Config, &Data);
  if (EFI_ERROR (Status)) {
    EFI_HII_POPUP_PROTOCOL *Popup;
    if (!EFI_ERROR (gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid,
                                        NULL, (VOID **)&Popup)))
      Popup->CreatePopup (Popup, EfiHiiPopupStyleError,
          EfiHiiPopupTypeOk, Setup->Hii,
          Status == EFI_TIMEOUT ? STRING_TOKEN (STR_TIMEOUT) :
                                  STRING_TOKEN (STR_SAVE_FAILED), NULL);
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
Callback (CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This,
          EFI_BROWSER_ACTION Action, EFI_QUESTION_ID Key, UINT8 Type,
          EFI_IFR_TYPE_VALUE *Value, EFI_BROWSER_ACTION_REQUEST *Request)
{
  if (!Request) return EFI_INVALID_PARAMETER;
  *Request = EFI_BROWSER_ACTION_REQUEST_NONE;
  if (Action != EFI_BROWSER_ACTION_CHANGED || Key != KEY_SAVE)
    return EFI_UNSUPPORTED;
  *Request = EFI_BROWSER_ACTION_REQUEST_SUBMIT;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Supported (EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
           EFI_DEVICE_PATH_PROTOCOL *Remaining)
{
  EFI_IP4_CONFIG2_PROTOCOL *Config;
  EFI_STATUS Status;
  Status = gBS->OpenProtocol (Controller, &gEfiDevicePathProtocolGuid,
      NULL, This->DriverBindingHandle, Controller,
      EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->OpenProtocol (Controller,
      &gEfiHiiConfigAccessProtocolGuid, NULL, This->DriverBindingHandle,
      Controller, EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
  if (!EFI_ERROR (Status)) return EFI_UNSUPPORTED;
  Status = gBS->OpenProtocol (Controller, &gEfiIp4Config2ProtocolGuid,
      (VOID **)&Config, This->DriverBindingHandle, Controller,
      EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (!EFI_ERROR (Status))
    gBS->CloseProtocol (Controller, &gEfiIp4Config2ProtocolGuid,
                        This->DriverBindingHandle, Controller);
  return Status;
}

STATIC EFI_STATUS EFIAPI
Start (EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
       EFI_DEVICE_PATH_PROTOCOL *Remaining)
{
  IP4_SETUP *Setup = AllocateZeroPool (sizeof (*Setup));
  EFI_STATUS Status;
  if (!Setup) return EFI_OUT_OF_RESOURCES;
  Status = gBS->OpenProtocol (Controller, &gEfiIp4Config2ProtocolGuid,
      (VOID **)&Setup->Config, This->DriverBindingHandle, Controller,
      EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) goto Free;
  Setup->Controller = Controller;
  Setup->Access.ExtractConfig = Extract;
  Setup->Access.RouteConfig = Route;
  Setup->Access.Callback = Callback;
  Status = gBS->InstallProtocolInterface (&Controller,
      &gEfiHiiConfigAccessProtocolGuid, EFI_NATIVE_INTERFACE,
      &Setup->Access);
  if (EFI_ERROR (Status)) goto Close;
  Setup->Hii = HiiAddPackages (&FormGuid, Controller,
                                Ip4ConfigDxeStrings, FormsBin, NULL);
  if (Setup->Hii) return EFI_SUCCESS;
  gBS->UninstallProtocolInterface (Controller,
      &gEfiHiiConfigAccessProtocolGuid, &Setup->Access);
  Status = EFI_OUT_OF_RESOURCES;
Close:
  gBS->CloseProtocol (Controller, &gEfiIp4Config2ProtocolGuid,
                      This->DriverBindingHandle, Controller);
Free:
  FreePool (Setup);
  return Status;
}

STATIC EFI_STATUS EFIAPI
Stop (EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
      UINTN ChildCount, EFI_HANDLE *Children)
{
  IP4_SETUP *Setup;
  EFI_STATUS Status;
  if (ChildCount) return EFI_INVALID_PARAMETER;
  Status = gBS->HandleProtocol (Controller,
      &gEfiHiiConfigAccessProtocolGuid, (VOID **)&Setup);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->UninstallProtocolInterface (Controller,
      &gEfiHiiConfigAccessProtocolGuid, &Setup->Access);
  if (EFI_ERROR (Status)) return Status;
  HiiRemovePackages (Setup->Hii);
  gBS->CloseProtocol (Controller, &gEfiIp4Config2ProtocolGuid,
                      This->DriverBindingHandle, Controller);
  FreePool (Setup);
  return EFI_SUCCESS;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL Binding = {
  Supported, Start, Stop, 0x10, NULL, NULL
};

EFI_STATUS EFIAPI
Ip4ConfigInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  return EfiLibInstallDriverBindingComponentName2 (
      Image, SystemTable, &Binding, Image, NULL, NULL);
}
