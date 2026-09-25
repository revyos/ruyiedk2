// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NetLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "Ip4Config.h"

STATIC EFI_STATUS
ReadData (EFI_IP4_CONFIG2_PROTOCOL *Config,
          EFI_IP4_CONFIG2_DATA_TYPE Type, VOID **Data, UINTN *Size)
{
  EFI_STATUS Status;
  *Data = NULL;
  *Size = 0;
  Status = Config->GetData (Config, Type, Size, NULL);
  if (Status == EFI_NOT_FOUND) {
    *Size = 0;
    return EFI_SUCCESS;
  }
  if (Status != EFI_BUFFER_TOO_SMALL) return Status;
  *Data = AllocatePool (*Size);
  if (!*Data) return EFI_OUT_OF_RESOURCES;
  Status = Config->GetData (Config, Type, Size, *Data);
  if (EFI_ERROR (Status)) {
    FreePool (*Data);
    *Data = NULL;
  }
  return Status;
}

STATIC VOID
FormatAddress (CHAR16 *Text, CONST EFI_IPv4_ADDRESS *Address)
{
  UnicodeSPrint (Text, IP4_TEXT_SIZE * sizeof (*Text), L"%u.%u.%u.%u",
      Address->Addr[0], Address->Addr[1],
      Address->Addr[2], Address->Addr[3]);
}

EFI_STATUS
Ip4Load (EFI_IP4_CONFIG2_PROTOCOL *Config, IP4_FORM_DATA *Data)
{
  EFI_IP4_CONFIG2_POLICY Policy;
  EFI_STATUS Status;
  VOID *Value;
  UINTN Size = sizeof (Policy);

  ZeroMem (Data, sizeof (*Data));
  Status = Config->GetData (Config, Ip4Config2DataTypePolicy,
                             &Size, &Policy);
  if (EFI_ERROR (Status)) return Status;
  if (Size != sizeof (Policy) || Policy >= Ip4Config2PolicyMax)
    return EFI_COMPROMISED_DATA;
  Data->Dhcp = Policy == Ip4Config2PolicyDhcp;
  // A DHCP lease is operational state, not an editable static setting.
  if (Data->Dhcp) return EFI_SUCCESS;

  Status = ReadData (Config, Ip4Config2DataTypeManualAddress,
                     &Value, &Size);
  if (EFI_ERROR (Status)) return Status;
  if (Size) {
    if (Size != sizeof (EFI_IP4_CONFIG2_MANUAL_ADDRESS)) {
      FreePool (Value);
      return EFI_UNSUPPORTED;
    }
    EFI_IP4_CONFIG2_MANUAL_ADDRESS *Manual = Value;
    FormatAddress (Data->Address, &Manual->Address);
    FormatAddress (Data->Mask, &Manual->SubnetMask);
  }
  if (Value) FreePool (Value);

  Status = ReadData (Config, Ip4Config2DataTypeGateway, &Value, &Size);
  if (EFI_ERROR (Status)) return Status;
  if (Size) {
    if (Size != sizeof (EFI_IPv4_ADDRESS)) {
      FreePool (Value);
      return EFI_UNSUPPORTED;
    }
    FormatAddress (Data->Gateway, Value);
  }
  if (Value) FreePool (Value);

  Status = ReadData (Config, Ip4Config2DataTypeDnsServer, &Value, &Size);
  if (EFI_ERROR (Status)) return Status;
  if (Size % sizeof (EFI_IPv4_ADDRESS) ||
      Size / sizeof (EFI_IPv4_ADDRESS) > IP4_DNS_COUNT) {
    if (Value) FreePool (Value);
    return EFI_UNSUPPORTED;
  }
  EFI_IPv4_ADDRESS *Dns = Value;
  UINTN Offset = 0;
  for (UINTN I = 0; I < Size / sizeof (*Dns); I++) {
    if (I) Data->Dns[Offset++] = L' ';
    FormatAddress (&Data->Dns[Offset], &Dns[I]);
    Offset += StrLen (&Data->Dns[Offset]);
  }
  if (Value) FreePool (Value);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseAddress (CONST CHAR16 *Text, EFI_IPv4_ADDRESS *Address)
{
  CHAR16 *End;
  RETURN_STATUS Status = StrToIpv4Address (Text, &End, Address, NULL);
  return RETURN_ERROR (Status) || *End ? EFI_INVALID_PARAMETER :
                                        EFI_SUCCESS;
}

STATIC UINT32
AddressValue (CONST EFI_IPv4_ADDRESS *Address)
{
  return ((UINT32)Address->Addr[0] << 24) |
         ((UINT32)Address->Addr[1] << 16) |
         ((UINT32)Address->Addr[2] << 8) | Address->Addr[3];
}

STATIC BOOLEAN
IsUnicast (CONST EFI_IPv4_ADDRESS *Address)
{
  return Address->Addr[0] != 0 && Address->Addr[0] != 127 &&
         Address->Addr[0] < 224;
}

STATIC EFI_STATUS
ParseDns (CONST CHAR16 *Text, EFI_IPv4_ADDRESS *Dns, UINTN *Count)
{
  *Count = 0;
  while (*Text) {
    CHAR16 Address[IP4_TEXT_SIZE];
    UINTN Length = 0;
    while (*Text == L' ') Text++;
    if (!*Text) break;
    if (*Count == IP4_DNS_COUNT) return EFI_INVALID_PARAMETER;
    while (*Text && *Text != L' ') {
      if (Length == IP4_TEXT_SIZE - 1) return EFI_INVALID_PARAMETER;
      Address[Length++] = *Text++;
    }
    Address[Length] = 0;
    if (EFI_ERROR (ParseAddress (Address, &Dns[*Count])) ||
        !IsUnicast (&Dns[*Count])) return EFI_INVALID_PARAMETER;
    for (UINTN I = 0; I < *Count; I++)
      if (!CompareMem (&Dns[I], &Dns[*Count], sizeof (*Dns)))
        return EFI_INVALID_PARAMETER;
    (*Count)++;
  }
  return EFI_SUCCESS;
}

// Wait for asynchronous address validation, not for a DHCP lease.
STATIC EFI_STATUS
SetData (EFI_IP4_CONFIG2_PROTOCOL *Config,
         EFI_IP4_CONFIG2_DATA_TYPE Type, UINTN Size, VOID *Data)
{
  VOID *Current;
  UINTN CurrentSize, Index;
  EFI_EVENT Events[2];
  EFI_STATUS Status = ReadData (Config, Type, &Current, &CurrentSize);
  if (EFI_ERROR (Status)) return Status;
  BOOLEAN Equal = CurrentSize == Size &&
                  (!Size || !CompareMem (Current, Data, Size));
  if (Current) FreePool (Current);
  if (Equal) return EFI_SUCCESS;

  Status = gBS->CreateEvent (0, TPL_APPLICATION, NULL, NULL, &Events[0]);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION,
                             NULL, NULL, &Events[1]);
  if (EFI_ERROR (Status)) goto CloseNotify;
  Status = Config->RegisterDataNotify (Config, Type, Events[0]);
  if (EFI_ERROR (Status)) goto CloseTimer;
  Status = gBS->SetTimer (Events[1], TimerRelative, 50000000);
  if (EFI_ERROR (Status)) goto Unregister;
  Status = Config->SetData (Config, Type, Size, Data);
  while (Status == EFI_NOT_READY) {
    Status = gBS->WaitForEvent (2, Events, &Index);
    if (EFI_ERROR (Status)) break;
    if (Index == 1) {
      Status = EFI_TIMEOUT;
      break;
    }
    Status = ReadData (Config, Type, &Current, &CurrentSize);
    if (!EFI_ERROR (Status)) {
      if (CurrentSize != Size ||
          (Size && CompareMem (Current, Data, Size)))
        Status = EFI_DEVICE_ERROR;
      if (Current) FreePool (Current);
    }
  }
Unregister:
  Config->UnregisterDataNotify (Config, Type, Events[0]);
CloseTimer:
  gBS->CloseEvent (Events[1]);
CloseNotify:
  gBS->CloseEvent (Events[0]);
  return Status;
}

EFI_STATUS
Ip4Save (EFI_IP4_CONFIG2_PROTOCOL *Config, CONST IP4_FORM_DATA *Data)
{
  EFI_IP4_CONFIG2_POLICY Policy;
  EFI_IP4_CONFIG2_MANUAL_ADDRESS Manual;
  EFI_IPv4_ADDRESS Gateway = {{0}}, Dns[IP4_DNS_COUNT];
  EFI_STATUS Status;
  UINTN DnsCount = 0;
  UINT32 Mask;

  if (Data->Dhcp > 1) return EFI_INVALID_PARAMETER;
  Policy = Data->Dhcp ? Ip4Config2PolicyDhcp : Ip4Config2PolicyStatic;
  if (Data->Dhcp)
    return SetData (Config, Ip4Config2DataTypePolicy,
                     sizeof (Policy), &Policy);

  // Validate the entire form before changing any protocol configuration.
  if (Data->Address[IP4_TEXT_SIZE - 1] ||
      Data->Mask[IP4_TEXT_SIZE - 1] ||
      Data->Gateway[IP4_TEXT_SIZE - 1] ||
      Data->Dns[IP4_DNS_SIZE - 1]) return EFI_INVALID_PARAMETER;
  if (EFI_ERROR (ParseAddress (Data->Address, &Manual.Address)) ||
      EFI_ERROR (ParseAddress (Data->Mask, &Manual.SubnetMask)) ||
      !IsUnicast (&Manual.Address)) return EFI_INVALID_PARAMETER;
  Mask = AddressValue (&Manual.SubnetMask);
  if (!Mask || NetGetMaskLength (Mask) > 32 ||
      !NetIp4IsUnicast (AddressValue (&Manual.Address), Mask))
    return EFI_INVALID_PARAMETER;
  if (Data->Gateway[0] &&
      EFI_ERROR (ParseAddress (Data->Gateway, &Gateway)))
    return EFI_INVALID_PARAMETER;
  if (AddressValue (&Gateway) &&
      (!IsUnicast (&Gateway) ||
       !NetIp4IsUnicast (AddressValue (&Gateway), Mask)))
    return EFI_INVALID_PARAMETER;
  Status = ParseDns (Data->Dns, Dns, &DnsCount);
  if (EFI_ERROR (Status)) return Status;

  Status = SetData (Config, Ip4Config2DataTypePolicy,
                    sizeof (Policy), &Policy);
  if (EFI_ERROR (Status)) return Status;
  Status = SetData (Config, Ip4Config2DataTypeManualAddress,
                    sizeof (Manual), &Manual);
  if (EFI_ERROR (Status)) return Status;
  Status = SetData (Config, Ip4Config2DataTypeGateway,
      AddressValue (&Gateway) ? sizeof (Gateway) : 0,
      AddressValue (&Gateway) ? &Gateway : NULL);
  if (EFI_ERROR (Status)) return Status;
  return SetData (Config, Ip4Config2DataTypeDnsServer,
                   DnsCount * sizeof (*Dns), DnsCount ? Dns : NULL);
}
