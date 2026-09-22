// SPDX-License-Identifier: BSD-2-Clause-Patent
// TH1520 factory MAC slots: GMAC0 at 0xb0, GMAC1 at 0xb6 (vendor BSP).
#include "Gmac.h"
#include <Library/PcdLib.h>
#include <Library/Th1520MacStoreLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/Th1520MacStore.h>
#include <Protocol/Th1520NvStore.h>
#include <Protocol/VariableLock.h>

#define EFUSE_BASE 0xffff210000ULL
#define EFUSE_CONTROL 0x00
#define EFUSE_ADDRESS 0x40
#define EFUSE_CHECK0 0x50
#define EFUSE_STATUS 0x70
#define EFUSE_DATA 0x80
#define EFUSE_ERRORS 0x4f70
#define EFUSE_COMMAND_FIELDS (BIT0 | BIT1 | BIT2 | (31U << 8) | BIT13)

STATIC EFI_STATUS
WaitForRead (VOID)
{
  UINTN Retry;
  UINT32 Status;
  for (Retry = 0; Retry < 10000; Retry++)
  {
    Status = MmioRead32 (EFUSE_BASE + EFUSE_STATUS);
    if (!(Status & BIT0))
    {
      return (Status & EFUSE_ERRORS) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}
EFI_STATUS
GmacReadFactoryAddress (GMAC_HARDWARE *Hw, UINT8 *Mac)
{
  // These are the READ command check words, not permanent protection bits.
  // Only command 1 (READ) is used. Never touch WDATA or issue a lock command.
  STATIC CONST UINT32 ReadCheck[4] = {0xa4c8c6de, 0xf4d7fb08, 0xc3f981d0, 0x32224e05};
  EFI_STATUS Status;
  UINT32 Control, Value;
  UINTN Word, Index;
  UINTN Offset = Hw->Index ? 0xb6 : 0xb0;
  UINT8 Bytes[8];
  Control = MmioRead32 (EFUSE_BASE + EFUSE_CONTROL);
  if (Control & BIT14)
  {
    return EFI_NOT_READY;
  }
  Control &= ~EFUSE_COMMAND_FIELDS;
  for (Word = 0; Word < 2; Word++)
  {
    Status = WaitForRead ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    for (Index = 0; Index < ARRAY_SIZE (ReadCheck); Index++)
    {
      MmioWrite32 (EFUSE_BASE + EFUSE_CHECK0 + Index * 4, ReadCheck[Index]);
    }
    MmioWrite32 (EFUSE_BASE + EFUSE_ADDRESS, Offset / 4 + Word);
    MmioWrite32 (EFUSE_BASE + EFUSE_CONTROL, Control | (1U << 8) | BIT0);
    Status = WaitForRead ();
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Value = MmioRead32 (EFUSE_BASE + EFUSE_DATA);
    for (Index = 0; Index < 4; Index++)
    {
      Bytes[Word * 4 + Index] = (UINT8)(Value >> (Index * 8));
    }
    // Clear the volatile read buffer with START unset.
    MmioWrite32 (EFUSE_BASE + EFUSE_CONTROL, Control | BIT1);
  }
  CopyMem (Mac, Bytes + Offset % 4, 6);
  return EFI_SUCCESS;
}

EFI_STATUS
GmacFallbackAddress (UINTN Index, UINT8 *Mac)
{
  STATIC UINT8 Base[6];
  STATIC BOOLEAN Ready, Locked;

  if (Index > 1 || Mac == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!Ready)
  {
    UINTN Size = sizeof (Base);
    UINT32 Attributes = 0;
    TH1520_MAC_STORE_PROTOCOL *Store;
    EFI_STATUS Status;
    if (PcdGetBool (PcdFirmwareOnEmmc))
    {
      Status = gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                     NULL, (VOID **)&Store);
      if (!EFI_ERROR (Status))
        Status = Store->Read (Base);
      Attributes = EFI_VARIABLE_NON_VOLATILE;
    }
    else
      Status = gRT->GetVariable (L"FallbackMac", &gTh1520NvStoreGuid,
                                 &Attributes, &Size, Base);
    if (!EFI_ERROR (Status) && Size == sizeof (Base) &&
        (Attributes & EFI_VARIABLE_NON_VOLATILE) &&
        (Base[0] & 3) == 2 && !(Base[5] & 1))
      Ready = TRUE;
  }
  if (!Ready)
  {
    EFI_STATUS Status = Th1520MacGenerate (Base);
    if (EFI_ERROR (Status))
      return Status;
    // A failed save still permits networking; BDS reports it as temporary.
    if (PcdGetBool (PcdFirmwareOnEmmc))
    {
      TH1520_MAC_STORE_PROTOCOL *Store;
      if (!EFI_ERROR (gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                         NULL, (VOID **)&Store)))
        Store->Write (Base);
    }
    else
      gRT->SetVariable (L"FallbackMac", &gTh1520NvStoreGuid,
          EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
          sizeof (Base), Base);
    Ready = TRUE;
  }
  if (!PcdGetBool (PcdFirmwareOnEmmc) && !Locked)
  {
    EDKII_VARIABLE_LOCK_PROTOCOL *Lock;
    if (!EFI_ERROR (gBS->LocateProtocol (&gEdkiiVariableLockProtocolGuid,
                                       NULL, (VOID **)&Lock)))
      Locked = !EFI_ERROR (Lock->RequestToLock (
          Lock, L"FallbackMac", &gTh1520NvStoreGuid));
  }
  CopyMem (Mac, Base, sizeof (Base));
  Mac[5] += Index;
  return EFI_SUCCESS;
}

EFI_STATUS
GmacReadSettings (TH1520_MAC_SETTINGS *Settings)
{
  EFI_STATUS Status;
  UINTN Size = sizeof (*Settings);
  UINT32 Attributes = 0;
  SetMem (Settings, Size, 0);
  if (PcdGetBool (PcdFirmwareOnEmmc))
  {
    TH1520_MAC_STORE_PROTOCOL *Store;
    Status = gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                   NULL, (VOID **)&Store);
    if (!EFI_ERROR (Status))
      Status = Store->GetSettings (Settings);
  }
  else
  {
    Status = gRT->GetVariable (L"MacSettings", &gTh1520NvStoreGuid,
                               &Attributes, &Size, Settings);
    if (Status == EFI_BUFFER_TOO_SMALL ||
        (!EFI_ERROR (Status) &&
         (Size != sizeof (*Settings) ||
          !(Attributes & EFI_VARIABLE_NON_VOLATILE))))
      Status = EFI_COMPROMISED_DATA;
  }
  if (EFI_ERROR (Status))
    SetMem (Settings, sizeof (*Settings), 0);
  else
  {
    Settings->OverrideMask &= 3;
    for (UINTN I = 0; I < 2; I++)
      if (!Th1520MacValid (Settings->Address[I]))
        Settings->OverrideMask &= ~(1U << I);
  }
  return Status;
}

EFI_STATUS EFIAPI
GmacWriteSettings (CONST TH1520_MAC_SETTINGS *Settings)
{
  if (!Th1520MacSettingsValid (Settings))
    return EFI_INVALID_PARAMETER;
  if (PcdGetBool (PcdFirmwareOnEmmc))
  {
    TH1520_MAC_STORE_PROTOCOL *Store;
    EFI_STATUS Status = gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                             NULL, (VOID **)&Store);
    return EFI_ERROR (Status) ? Status : Store->SetSettings (Settings);
  }
  return gRT->SetVariable (L"MacSettings", &gTh1520NvStoreGuid,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
      sizeof (*Settings), (VOID *)Settings);
}
