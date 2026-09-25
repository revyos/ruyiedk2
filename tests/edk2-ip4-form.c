// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/Th1520Pkg/Drivers/Ip4ConfigDxe/Ip4Config.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <Library/NetLib.h>

typedef struct {
  EFI_IP4_CONFIG2_PROTOCOL Protocol;
  UINT8 Data[Ip4Config2DataTypeMaximum][128];
  UINTN Size[Ip4Config2DataTypeMaximum];
  UINTN Sets;
} NIC;
static UINTN Events, Notifications, Waits;
static BOOLEAN Async, Timeout, Pending, Mismatch;
static EFI_IP4_CONFIG2_DATA_TYPE FailType;
static NIC *PendingNic;
static EFI_IP4_CONFIG2_MANUAL_ADDRESS PendingAddress;
static IP4_FORM_DATA Submitted;
static BOOLEAN Partial;

VOID *EFIAPI AllocatePool (UINTN N) { return malloc (N); }
VOID *EFIAPI AllocateZeroPool (UINTN N) { return calloc (1, N); }
VOID EFIAPI FreePool (VOID *P) { free (P); }
VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset (P, 0, N); }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return N ? memcpy (D, S, N) : D; }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
UINTN EFIAPI StrLen (CONST CHAR16 *S)
{ UINTN N = 0; while (S[N]) N++; return N; }
UINTN EFIAPI UnicodeSPrint (CHAR16 *Out, UINTN Size,
                          CONST CHAR16 *Format, ...)
{
  char Text[32];
  va_list Args;
  va_start (Args, Format);
  unsigned A = va_arg (Args, unsigned), B = va_arg (Args, unsigned);
  unsigned C = va_arg (Args, unsigned), D = va_arg (Args, unsigned);
  va_end (Args);
  int N = snprintf (Text, sizeof (Text), "%u.%u.%u.%u", A, B, C, D);
  assert ((UINTN)(N + 1) * 2 <= Size);
  for (int I = 0; I <= N; I++) Out[I] = Text[I];
  return N;
}
RETURN_STATUS EFIAPI
StrToIpv4Address (CONST CHAR16 *Text, CHAR16 **End,
                  IPv4_ADDRESS *Ip, UINT8 *Prefix)
{
  char Buffer[32];
  UINTN N = StrLen (Text);
  if (N >= sizeof (Buffer)) return RETURN_INVALID_PARAMETER;
  for (UINTN I = 0; I <= N; I++) Buffer[I] = Text[I];
  if (inet_pton (AF_INET, Buffer, Ip) != 1)
    return RETURN_INVALID_PARAMETER;
  *End = (CHAR16 *)Text + N;
  return RETURN_SUCCESS;
}
static EFI_STATUS EFIAPI
Get (EFI_IP4_CONFIG2_PROTOCOL *This, EFI_IP4_CONFIG2_DATA_TYPE Type,
     UINTN *Size, VOID *Data)
{
  NIC *Nic = (NIC *)This;
  // Saved settings must not depend on an active interface.
  assert (Type != Ip4Config2DataTypeInterfaceInfo);
  if (Type == Ip4Config2DataTypeManualAddress && Pending)
    return EFI_NOT_READY;
  if (!Nic->Size[Type]) return EFI_NOT_FOUND;
  UINTN Available = *Size;
  *Size = Nic->Size[Type];
  if (Available < *Size) return EFI_BUFFER_TOO_SMALL;
  memcpy (Data, Nic->Data[Type], *Size);
  return EFI_SUCCESS;
}
static void Store (NIC *Nic, EFI_IP4_CONFIG2_DATA_TYPE Type,
                   UINTN Size, VOID *Data)
{
  assert (Size <= sizeof (Nic->Data[Type]));
  Nic->Size[Type] = Size;
  if (Size) memcpy (Nic->Data[Type], Data, Size);
}
static EFI_STATUS EFIAPI
Set (EFI_IP4_CONFIG2_PROTOCOL *This, EFI_IP4_CONFIG2_DATA_TYPE Type,
     UINTN Size, VOID *Data)
{
  NIC *Nic = (NIC *)This;
  assert (Notifications == 1);
  Nic->Sets++;
  if (Type == FailType) return EFI_DEVICE_ERROR;
  if (Type == Ip4Config2DataTypeManualAddress && Async) {
    PendingAddress = *(EFI_IP4_CONFIG2_MANUAL_ADDRESS *)Data;
    PendingNic = Nic;
    Pending = TRUE;
    return EFI_NOT_READY;
  }
  Store (Nic, Type, Size, Data);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Register (EFI_IP4_CONFIG2_PROTOCOL *This,
          EFI_IP4_CONFIG2_DATA_TYPE Type, EFI_EVENT Event)
{ Notifications++; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI
Unregister (EFI_IP4_CONFIG2_PROTOCOL *This,
            EFI_IP4_CONFIG2_DATA_TYPE Type, EFI_EVENT Event)
{ assert (Notifications == 1); Notifications--; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI
Event (UINT32 Type, EFI_TPL Tpl, EFI_EVENT_NOTIFY Notify,
       VOID *Context, EFI_EVENT *Out)
{ *Out = malloc (1); Events++; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Close (EFI_EVENT Event)
{ free (Event); Events--; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Timer (EFI_EVENT Event,
                                EFI_TIMER_DELAY Type, UINT64 Time)
{ assert (Time == 50000000); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI
Wait (UINTN Count, EFI_EVENT *Event, UINTN *Index)
{
  assert (Count == 2 && Pending);
  Waits++;
  *Index = Timeout ? 1 : 0;
  if (!Timeout) {
    Pending = FALSE;
    if (Mismatch) PendingAddress.Address.Addr[3]++;
    Store (PendingNic, Ip4Config2DataTypeManualAddress,
            sizeof (PendingAddress), &PendingAddress);
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Locate (EFI_GUID *Guid, VOID *Registration, VOID **Interface)
{ return EFI_NOT_FOUND; }
static EFI_BOOT_SERVICES Boot = {
  .CreateEvent = Event, .CloseEvent = Close, .SetTimer = Timer,
  .WaitForEvent = Wait, .LocateProtocol = Locate
};
EFI_BOOT_SERVICES *gBS = &Boot;
EFI_GUID gEfiHiiPopupProtocolGuid;

BOOLEAN EFIAPI HiiIsConfigHdrMatch (CONST EFI_STRING Config,
                                   CONST EFI_GUID *Guid,
                                   CONST CHAR16 *Name)
{ return Config[0] == L'v'; }
static EFI_STATUS EFIAPI
ConfigToBlock (CONST EFI_HII_CONFIG_ROUTING_PROTOCOL *This,
               CONST EFI_STRING Text, UINT8 *Block, UINTN *Size,
               EFI_STRING *Progress)
{
  assert (*Size == sizeof (Submitted));
  if (Partial)
    memcpy (((IP4_FORM_DATA *)Block)->Dns, Submitted.Dns,
             sizeof (Submitted.Dns));
  else
    memcpy (Block, &Submitted, sizeof (Submitted));
  return EFI_SUCCESS;
}
static EFI_HII_CONFIG_ROUTING_PROTOCOL Routing = {
  .ConfigToBlock = ConfigToBlock
};
EFI_HII_CONFIG_ROUTING_PROTOCOL *gHiiConfigRouting = &Routing;

static void Text (CHAR16 *Out, const char *In)
{ do { *Out++ = *In; } while (*In++); }
static void CheckText (CONST CHAR16 *Actual, const char *Expected)
{
  for (; *Expected; Expected++, Actual++) assert (*Actual == *Expected);
  assert (!*Actual);
}
static void Init (NIC *Nic)
{
  memset (Nic, 0, sizeof (*Nic));
  Nic->Protocol = (EFI_IP4_CONFIG2_PROTOCOL){Set, Get,
                                           Register, Unregister};
  EFI_IP4_CONFIG2_POLICY Policy = Ip4Config2PolicyDhcp;
  Store (Nic, Ip4Config2DataTypePolicy, sizeof (Policy), &Policy);
}
static void CheckClosed (void)
{ assert (!Events && !Notifications); }

int main (void)
{
  NIC Nic, Peer;
  IP4_FORM_DATA Form;
  EFI_STRING Progress;
  EFI_BROWSER_ACTION_REQUEST Request;
  Init (&Nic);
  Init (&Peer);
  FailType = Ip4Config2DataTypeMaximum;
  IP4_SETUP Setup = {.Config = &Nic.Protocol};
  assert (!Ip4Load (&Nic.Protocol, &Form) && Form.Dhcp);
  Form.Dhcp = FALSE;
  Text (Form.Address, "192.0.2.8");
  Text (Form.Mask, "255.255.255.0");
  Text (Form.Gateway, "192.0.2.1");
  Text (Form.Dns, "192.0.2.53 198.51.100.53");
  Submitted = Form;
  assert (Callback (&Setup.Access, EFI_BROWSER_ACTION_CHANGED, 7,
                    0, NULL, &Request) == EFI_UNSUPPORTED);
  assert (!Callback (&Setup.Access, EFI_BROWSER_ACTION_CHANGED,
                     KEY_SAVE, 0, NULL, &Request));
  assert (Request == EFI_BROWSER_ACTION_REQUEST_SUBMIT && !Nic.Sets);
  assert (!Ip4Load (&Nic.Protocol, &Form) && Form.Dhcp);
  // F10 and the Save action both submit through RouteConfig.
  assert (!Route (&Setup.Access, L"valid", &Progress));
  assert (Nic.Sets == 4);
  assert (!Ip4Load (&Nic.Protocol, &Form) && !Form.Dhcp);
  CheckText (Form.Address, "192.0.2.8");
  CheckText (Form.Mask, "255.255.255.0");
  CheckText (Form.Gateway, "192.0.2.1");
  CheckText (Form.Dns, "192.0.2.53 198.51.100.53");
  assert (!Ip4Save (&Nic.Protocol, &Form) && Nic.Sets == 4);
  assert (!Ip4Load (&Peer.Protocol, &Form) && Form.Dhcp && !Peer.Sets);
  // Partial requests preserve fields not included in the request.
  Submitted.Dns[0] = 0;
  Partial = TRUE;
  assert (!Route (&Setup.Access, L"valid", &Progress));
  Partial = FALSE;
  assert (Nic.Sets == 5);
  assert (!Ip4Load (&Nic.Protocol, &Form) && !Form.Dns[0]);
  CheckText (Form.Address, "192.0.2.8");
  Form.Gateway[0] = 0;
  assert (!Ip4Save (&Nic.Protocol, &Form) && Nic.Sets == 6);
  assert (!Ip4Load (&Nic.Protocol, &Form) && !Form.Gateway[0]);
  IP4_FORM_DATA Valid = Form;
  Text (Form.Dns, "bad");
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_INVALID_PARAMETER);
  Form = Valid;
  memset (Form.Address, 'x', sizeof (Form.Address));
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_INVALID_PARAMETER);
  Form = Valid;
  Text (Form.Mask, "255.0.255.0");
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_INVALID_PARAMETER);
  assert (Nic.Sets == 6);
  Form = Valid;
  Async = TRUE;
  Text (Form.Address, "192.0.2.9");
  assert (!Ip4Save (&Nic.Protocol, &Form) && Waits == 1);
  assert (!Ip4Load (&Nic.Protocol, &Form));
  CheckText (Form.Address, "192.0.2.9");
  CheckClosed ();
  Text (Form.Address, "192.0.2.10");
  Mismatch = TRUE;
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_DEVICE_ERROR);
  CheckClosed ();
  Mismatch = FALSE;
  Timeout = TRUE;
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_TIMEOUT);
  CheckClosed ();
  assert (Ip4Load (&Nic.Protocol, &Submitted) == EFI_NOT_READY);
  Pending = Timeout = Async = FALSE;
  FailType = Ip4Config2DataTypeDnsServer;
  Text (Form.Dns, "192.0.2.53");
  assert (Ip4Save (&Nic.Protocol, &Form) == EFI_DEVICE_ERROR);
  CheckClosed ();
  FailType = Ip4Config2DataTypeMaximum;
  Form.Dhcp = TRUE;
  UINTN Before = Nic.Sets;
  assert (!Ip4Save (&Nic.Protocol, &Form));
  assert (Nic.Sets == Before + 1);
  assert (!Ip4Load (&Nic.Protocol, &Form) && Form.Dhcp);
  CheckClosed ();
  puts ("IPv4: staged edits, per-interface readback and errors OK");
}

// Services outside the protocol/form submission paths under test.
EFI_GUID gEfiDevicePathProtocolGuid;
EFI_GUID gEfiHiiConfigAccessProtocolGuid;
EFI_GUID gEfiIp4Config2ProtocolGuid;
EFI_GUID gEfiRngAlgorithmSp80090Ctr256Guid;
EFI_GUID gEfiRngAlgorithmSp80090Hmac256Guid;
EFI_GUID gEfiRngAlgorithmSp80090Hash256Guid;
EFI_GUID gEfiRngAlgorithmArmRndr;
EFI_GUID gEfiRngAlgorithmRaw;
UINT8 FormsBin[1], Ip4ConfigDxeStrings[1];
CHAR16 *EFIAPI StrStr (CONST CHAR16 *S, CONST CHAR16 *Needle)
{ assert (0); return NULL; }
UINTN EFIAPI StrSize (CONST CHAR16 *S)
{ return (StrLen (S) + 1) * sizeof (*S); }
EFI_STRING EFIAPI HiiConstructConfigHdr (
  CONST EFI_GUID *Guid, CONST CHAR16 *Name, EFI_HANDLE Handle)
{ assert (0); return NULL; }
EFI_HII_HANDLE EFIAPI HiiAddPackages (
  CONST EFI_GUID *Guid, EFI_HANDLE Handle, ...)
{ assert (0); return NULL; }
VOID EFIAPI HiiRemovePackages (EFI_HII_HANDLE Handle) { assert (0); }
