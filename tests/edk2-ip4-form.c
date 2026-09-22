// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Ip4Impl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>

EFI_STATUS Ip4Config2ConvertConfigNvDataToIfrNvData (
  IP4_CONFIG2_INSTANCE *Instance, IP4_CONFIG2_IFR_NVDATA *Form);
EFI_STATUS Ip4Config2ConvertIfrNvDataToConfigNvData (
  IP4_CONFIG2_IFR_NVDATA *Form, IP4_CONFIG2_INSTANCE *Instance);

EFI_STATUS EFIAPI Ip4FormCallback (
  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL *This, EFI_BROWSER_ACTION Action,
  EFI_QUESTION_ID Question, UINT8 Type, EFI_IFR_TYPE_VALUE *Value,
  EFI_BROWSER_ACTION_REQUEST *Request);
static IP4_CONFIG2_IFR_NVDATA Browser;
EFI_GUID gIp4Config2NvDataGuid;
BOOLEAN EFIAPI
HiiGetBrowserData (CONST EFI_GUID *Guid, CONST CHAR16 *Name,
                   UINTN Size, UINT8 *Data)
{ assert (Size == sizeof (Browser)); memcpy (Data, &Browser, Size);
  return TRUE; }
static EFI_IP4_CONFIG2_POLICY Policy;
static EFI_IP4_CONFIG2_MANUAL_ADDRESS Address = {
  {{192, 0, 2, 8}}, {{255, 255, 255, 0}}
};
static BOOLEAN HaveAddress, HaveGateway, HaveDns;
static EFI_IPv4_ADDRESS Gateway = {{192, 0, 2, 1}};
static EFI_IPv4_ADDRESS Dns = {{192, 0, 2, 53}};
static UINTN Writes, Sets, Records, LastSize;
static UINT8 LastValue[256];
static BOOLEAN Unchanged;
static EFI_STATUS WriteStatus;
EFI_GUID gEfiIp4Config2ProtocolGuid;

VOID *EFIAPI AllocateZeroPool (UINTN N) { return calloc (1, N); }
VOID *EFIAPI AllocatePool (UINTN N) { return malloc (N); }
VOID EFIAPI FreePool (VOID *P) { free (P); }
VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset (P, 0, N); }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return N ? memcpy (D, S, N) : D; }
UINTN EFIAPI StrLen (CONST CHAR16 *S)
{ UINTN N = 0; while (S[N]) N++; return N; }
UINTN EFIAPI UnicodeSPrint (CHAR16 *Out, UINTN Size,
                          CONST CHAR16 *Format, ...)
{
  char Text[32];
  va_list Args;
  va_start (Args, Format);
  int A = va_arg (Args, int), B = va_arg (Args, int);
  int Cc = va_arg (Args, int), D = va_arg (Args, int);
  va_end (Args);
  int N = snprintf (Text, sizeof (Text), "%d.%d.%d.%d", A, B, Cc, D);
  assert ((UINTN)(N + 1) * 2 <= Size);
  for (int I = 0; I <= N; I++) Out[I] = Text[I];
  return N;
}
UINT16 EFIAPI NetblockChecksum (UINT8 *Data, UINT32 Len)
{ (void)Data; (void)Len; return 0; }
UINT32 EFIAPI SwapBytes32 (UINT32 V) { return __builtin_bswap32 (V); }
RETURN_STATUS EFIAPI
StrToIpv4Address (CONST CHAR16 *Text, CHAR16 **End,
                  IPv4_ADDRESS *Ip, UINT8 *Prefix)
{
  char Buf[32];
  UINTN N = StrLen (Text);
  if (N >= sizeof (Buf)) return RETURN_INVALID_PARAMETER;
  for (UINTN I = 0; I <= N; I++) Buf[I] = Text[I];
  if (inet_pton (AF_INET, Buf, Ip) != 1) return RETURN_INVALID_PARAMETER;
  *End = (CHAR16 *)Text + N;
  return RETURN_SUCCESS;
}
BOOLEAN EFIAPI NetIp4IsUnicast (IP4_ADDR Ip, IP4_ADDR Mask)
{ return (Ip & ~Mask) != 0 && (Ip & ~Mask) != ~Mask; }
BOOLEAN Ip4StationAddressValid (IP4_ADDR Ip, IP4_ADDR Mask)
{ return NetIp4IsUnicast (Ip, Mask); }
VOID EFIAPI CreatePopUp (UINTN Attr, EFI_INPUT_KEY *Key, ...)
{ }
static EFI_STATUS EFIAPI
Event (UINT32 Type, EFI_TPL Tpl, EFI_EVENT_NOTIFY Notify,
       VOID *Context, EFI_EVENT *Out)
{ *Out = (EFI_EVENT)1; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Close (EFI_EVENT Event) { return EFI_SUCCESS; }
static EFI_BOOT_SERVICES Boot = {
  .CreateEvent = Event, .CloseEvent = Close
};
EFI_BOOT_SERVICES *gBS = &Boot;
static EFI_STATUS EFIAPI
Notify (EFI_IP4_CONFIG2_PROTOCOL *This, EFI_IP4_CONFIG2_DATA_TYPE Type,
        EFI_EVENT Event)
{ return EFI_SUCCESS; }
static EFI_STATUS EFIAPI
Write (CHAR16 *Name, EFI_GUID *Guid, UINT32 Attr, UINTN Size, VOID *Data)
{
  assert (Size <= sizeof (LastValue));
  Unchanged = Size == LastSize && !memcmp (LastValue, Data, Size);
  memcpy (LastValue, Data, Size);
  LastSize = Size;
  Writes++;
  Records = ((IP4_CONFIG2_VARIABLE *)Data)->DataRecordCount;
  return WriteStatus;
}
static EFI_RUNTIME_SERVICES Runtime = {.SetVariable = Write};
EFI_RUNTIME_SERVICES *gRT = &Runtime;

static EFI_STATUS EFIAPI
Get (EFI_IP4_CONFIG2_PROTOCOL *This, EFI_IP4_CONFIG2_DATA_TYPE Type,
     UINTN *Size, VOID *Data)
{
  VOID *Value;
  UINTN N;
  switch (Type) {
  case Ip4Config2DataTypePolicy:
    Value = &Policy; N = sizeof (Policy); break;
  case Ip4Config2DataTypeManualAddress:
    if (!HaveAddress) return EFI_NOT_FOUND;
    Value = &Address; N = sizeof (Address); break;
  case Ip4Config2DataTypeGateway:
    if (!HaveGateway) return EFI_NOT_FOUND;
    Value = &Gateway; N = sizeof (Gateway); break;
  case Ip4Config2DataTypeDnsServer:
    if (!HaveDns) return EFI_NOT_FOUND;
    Value = &Dns; N = sizeof (Dns); break;
  default:
    assert (!"Form must not read live interface information");
    return EFI_UNSUPPORTED;
  }
  if (*Size < N) { *Size = N; return EFI_BUFFER_TOO_SMALL; }
  *Size = N;
  memcpy (Data, Value, N);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Set (EFI_IP4_CONFIG2_PROTOCOL *This, EFI_IP4_CONFIG2_DATA_TYPE Type,
     UINTN Size, VOID *Data)
{
  IP4_CONFIG2_INSTANCE *Instance =
    IP4_CONFIG2_INSTANCE_FROM_PROTOCOL (This);
  Sets++;
  Instance->DataItem[Type].Data.Ptr = Data;
  Instance->DataItem[Type].DataSize = Size;
  Instance->DataItem[Type].Status = Size ? EFI_SUCCESS : EFI_NOT_FOUND;
  // Model protocol writes during form application; these must be batched.
  return Ip4Config2WriteConfigData (L"test", Instance);
}
static void CheckText (CHAR16 *Actual, const char *Expected)
{
  for (; *Expected; Expected++, Actual++) assert (*Actual == *Expected);
  assert (!*Actual);
}
int main (void)
{
  IP4_SERVICE Service = {0};
  IP4_CONFIG2_INSTANCE *Instance = &Service.Ip4Config2Instance;
  IP4_CONFIG2_IFR_NVDATA Form;
  Service.Signature = IP4_SERVICE_SIGNATURE;
  Instance->Signature = IP4_CONFIG2_INSTANCE_SIGNATURE;
  for (UINTN I = 0; I < Ip4Config2DataTypeMaximum; I++)
    Instance->DataItem[I].Status = EFI_NOT_FOUND;
  Instance->Ip4Config2.GetData = Get;
  Instance->Ip4Config2.SetData = Set;
  Instance->Ip4Config2.RegisterDataNotify = Notify;
  Instance->Ip4Config2.UnregisterDataNotify = Notify;
  Service.MacString = L"test";
  // No configured interface exists: reopening must still show the policy.
  Policy = Ip4Config2PolicyDhcp;
  memset (&Form, 0xa5, sizeof (Form));
  assert (!Ip4Config2ConvertConfigNvDataToIfrNvData (Instance, &Form));
  assert (Form.Configure && Form.DhcpEnable && !Form.StationAddress[0]);
  Policy = Ip4Config2PolicyStatic;
  assert (!Ip4Config2ConvertConfigNvDataToIfrNvData (Instance, &Form));
  assert (!Form.Configure && !Form.DhcpEnable);
  HaveAddress = TRUE;
  assert (!Ip4Config2ConvertConfigNvDataToIfrNvData (Instance, &Form));
  assert (Form.Configure && !Form.DhcpEnable);
  CheckText (Form.StationAddress, "192.0.2.8");
  CheckText (Form.SubnetMask, "255.255.255.0");
  CheckText (Form.GatewayAddress, "0.0.0.0");
  assert (!Form.DnsAddress[0]);
  HaveGateway = HaveDns = TRUE;
  assert (!Ip4Config2ConvertConfigNvDataToIfrNvData (Instance, &Form));
  CheckText (Form.GatewayAddress, "192.0.2.1");
  CheckText (Form.DnsAddress, "192.0.2.53");
  Browser = Form;
  EFI_BROWSER_ACTION_REQUEST Request = EFI_BROWSER_ACTION_REQUEST_NONE;
  assert (!Ip4FormCallback (NULL, EFI_BROWSER_ACTION_CHANGED,
                           KEY_DHCP_ENABLE, 0, NULL, &Request));
  assert (!Ip4FormCallback (NULL, EFI_BROWSER_ACTION_CHANGED,
                           KEY_SAVE_CHANGES, 0, NULL, &Request));
  assert (Request == EFI_BROWSER_ACTION_REQUEST_SUBMIT);
  assert (Writes == 0 && Sets == 0);
  assert (!Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance));
  assert (Writes == 1 && Sets == 4 && !Instance->DeferNvWrite);
  assert (Records == 4);
  assert (!Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance));
  assert (Writes == 2 && Sets == 8 && Unchanged);
  Form.DnsAddress[0] = 0;
  assert (!Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance));
  assert (Writes == 3 && Sets == 12 && Records == 3);
  Writes = Sets = 0;
  Form.DhcpEnable = TRUE;
  assert (!Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance));
  assert (Writes == 1 && Sets == 1 && !Instance->DeferNvWrite);
  WriteStatus = EFI_DEVICE_ERROR;
  assert (Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance) ==
          EFI_DEVICE_ERROR);
  assert (Writes == 2 && !Instance->DeferNvWrite);
  Form.DhcpEnable = FALSE;
  Form.StationAddress[0] = 'x';
  assert (Ip4Config2ConvertIfrNvDataToConfigNvData (&Form, Instance) ==
          EFI_INVALID_PARAMETER);
  assert (Writes == 2 && !Instance->DeferNvWrite);
  FreePool (Instance->Ip4NvData.ManualAddress);
  FreePool (Instance->Ip4NvData.GatewayAddress);
  FreePool (Instance->Ip4NvData.DnsAddress);
  puts ("IPv4 form: saved values, no edit writes, one synchronous commit");
}
