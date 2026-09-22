// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/PcdLib.h>
#include <Library/Th1520NvStoreLib.h>
#include <Library/Th1520SpiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Th1520MacStore.h>
#include <Protocol/VariableLock.h>

typedef struct {
  TH1520_NV_STORE Store;
  EFI_STATUS Status;
  UINTN Length;
  BOOLEAN Busy;
  UINT8 Cache[TH1520_NV_DATA_SIZE];
  UINT8 Next[TH1520_NV_DATA_SIZE];
  UINT8 Value[TH1520_NV_DATA_SIZE];
  UINT8 Backup[TH1520_NV_DATA_SIZE];
  UINT8 Scratch[TH1520_NV_DATA_SIZE];
  CHAR16 Name[TH1520_NV_DATA_SIZE / 2];
} NV_CONTEXT;
STATIC NV_CONTEXT *Nv;
STATIC EFI_SET_VARIABLE OriginalSet;
STATIC EFI_GET_VARIABLE OriginalGet;
STATIC EFI_GET_NEXT_VARIABLE_NAME OriginalNext;
STATIC EFI_STATUS Snapshot (VOID);
STATIC EFI_HANDLE DriverHandle;
STATIC VOID *Registration;

STATIC EFI_STATUS EFIAPI
SetVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 Attributes,
             UINTN Size, VOID *Data)
{
  UINT32 OldAttributes = 0, NewAttributes = 0;
  UINTN OldSize = sizeof (Nv->Backup), NewSize = sizeof (Nv->Value);
  EFI_STATUS Status, OldStatus;
  BOOLEAN Runtime = EfiAtRuntime (), Persistent;
  EFI_TPL Tpl = TPL_APPLICATION;
  if (!Name || !Guid)
    return EFI_INVALID_PARAMETER;
  if (Runtime && CompareGuid (Guid, &gTh1520NvStoreGuid) &&
      (!StrCmp (Name, L"FallbackMac") ||
       !StrCmp (Name, L"MacSettings")))
    return EFI_WRITE_PROTECTED;
  if (!Runtime)
    Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Nv->Busy)
  {
    Status = EFI_NOT_READY;
    goto Done;
  }
  Nv->Busy = TRUE;
  OldStatus = OriginalGet (Name, Guid, &OldAttributes,
                           &OldSize, Nv->Backup);
  if (OldStatus != EFI_NOT_FOUND && EFI_ERROR (OldStatus))
  {
    Status = OldStatus;
    goto Unlock;
  }
  Persistent = ((Attributes | OldAttributes) &
                EFI_VARIABLE_NON_VOLATILE) != 0;
  if (Persistent && EFI_ERROR (Nv->Status))
  {
    Status = Nv->Status;
    goto Unlock;
  }
  Status = OriginalSet (Name, Guid, Attributes, Size, Data);
  if (EFI_ERROR (Status) || !Persistent)
    goto Unlock;
  Status = OriginalGet (Name, Guid, &NewAttributes, &NewSize, Nv->Value);
  if (Status == EFI_NOT_FOUND)
  {
    NewSize = 0;
    Status = EFI_SUCCESS;
  }
  if (!EFI_ERROR (Status) &&
      !(OldStatus == EFI_SUCCESS && OldSize == NewSize &&
        OldAttributes == NewAttributes &&
        !CompareMem (Nv->Backup, Nv->Value, NewSize)))
    Status = Snapshot ();
  if (EFI_ERROR (Status))
  {
    // An unsuccessful durable write must not leave an acknowledged value.
    if (OldStatus == EFI_NOT_FOUND)
      OriginalSet (Name, Guid, 0, 0, NULL);
    else
      OriginalSet (Name, Guid, OldAttributes, OldSize, Nv->Backup);
  }
Unlock:
  Nv->Busy = FALSE;
Done:
  if (!Runtime)
    gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC EFI_STATUS
Replay (VOID)
{
  TH1520_NV_RECORD R;
  for (UINTN Offset = 0; Offset < Nv->Length; Offset += R.Size)
  {
    EFI_GUID Guid;
    EFI_STATUS Status = Th1520NvRecord (Nv->Cache, Nv->Length, Offset, &R);
    if (EFI_ERROR (Status))
      return Status;
    CopyMem (&Guid, R.Guid, sizeof (Guid));
    CopyMem (Nv->Name, R.Name, R.NameSize);
    // Migrate the prototype's runtime-visible MAC without a delete window.
    if (CompareGuid (&Guid, &gTh1520NvStoreGuid) &&
        (!StrCmp (Nv->Name, L"FallbackMac") ||
         !StrCmp (Nv->Name, L"MacSettings")))
      R.Attributes = EFI_VARIABLE_NON_VOLATILE |
                     EFI_VARIABLE_BOOTSERVICE_ACCESS;
    Status = OriginalSet (Nv->Name, &Guid, R.Attributes,
                          R.DataSize, (VOID *)R.Data);
    if (EFI_ERROR (Status))
      return Status;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Snapshot (VOID)
{
  EFI_GUID Guid = {0};
  UINTN Length = 0;
  if (EfiAtRuntime ())
  {
    // Runtime enumeration cannot see boot-services-only NV variables.
    TH1520_NV_RECORD R;
    for (UINTN At = 0; At < Nv->Length; At += R.Size)
    {
      EFI_STATUS Status = Th1520NvRecord (Nv->Cache, Nv->Length, At, &R);
      if (EFI_ERROR (Status))
        return Status;
      if (!(R.Attributes & EFI_VARIABLE_RUNTIME_ACCESS))
      {
        CopyMem (Nv->Next + Length, Nv->Cache + At, R.Size);
        Length += R.Size;
      }
    }
  }
  Nv->Name[0] = 0;
  while (TRUE)
  {
    UINTN NameSize = sizeof (Nv->Name), Size = sizeof (Nv->Value);
    UINT32 Attributes;
    EFI_STATUS Status = OriginalNext (&NameSize, Nv->Name,
                                                 &Guid);
    if (Status == EFI_NOT_FOUND)
      break;
    if (EFI_ERROR (Status))
      return Status;
    Status = OriginalGet (Nv->Name, &Guid, &Attributes, &Size, Nv->Value);
    if (EFI_ERROR (Status))
      return Status;
    if (!(Attributes & EFI_VARIABLE_NON_VOLATILE))
      continue;
    Status = Th1520NvUpdate (Nv->Next, &Length, Nv->Name, &Guid,
                             Attributes, Size, Nv->Value);
    if (EFI_ERROR (Status))
      return Status;
  }
  EFI_STATUS Status = EFI_SUCCESS;
  if (Length != Nv->Length || CompareMem (Nv->Cache, Nv->Next, Length))
  {
    Status = EfiAtRuntime () ? Th1520SpiRuntimeInit () :
                              Th1520SpiInitNative ();
    if (!EFI_ERROR (Status))
      Status = Th1520NvSave (&Nv->Store, Nv->Next, Length);
  }
  if (!EFI_ERROR (Status))
  {
    Nv->Length = Length;
    CopyMem (Nv->Cache, Nv->Next, Length);
  }
  return Status;
}

STATIC VOID EFIAPI
AddressChange (EFI_EVENT Event, VOID *Context)
{
  EfiConvertPointer (0, (VOID **)&OriginalSet);
  EfiConvertPointer (0, (VOID **)&OriginalGet);
  EfiConvertPointer (0, (VOID **)&OriginalNext);
  EfiConvertPointer (0, &Nv->Store.Scratch);
  EfiConvertPointer (0, (VOID **)&Nv->Store.Read);
  EfiConvertPointer (0, (VOID **)&Nv->Store.Write);
  EfiConvertPointer (0, (VOID **)&Nv->Store.Erase);
  EFI_STATUS Status = Th1520SpiConvertPointers (EfiConvertPointer);
  if (EFI_ERROR (Status))
    Nv->Status = Status;
  EfiConvertPointer (0, (VOID **)&Nv);
}

STATIC VOID EFIAPI
PrepareRuntime (EFI_EVENT Event, VOID *Context)
{
  // A Fastboot application may have temporarily selected GPIO chip select.
  // Restore native CS before handing the controller resources to the OS.
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = Th1520SpiInitNative ();
}

STATIC VOID EFIAPI
MacReady (EFI_EVENT Event, VOID *Context)
{
  TH1520_MAC_STORE_PROTOCOL *Mac;
  if (!EFI_ERROR (gBS->LocateProtocol (&gTh1520MacStoreProtocolGuid,
                                     NULL, (VOID **)&Mac)))
  {
    gBS->InstallProtocolInterface (&DriverHandle,
        &gTh1520NvStoreReadyProtocolGuid, EFI_NATIVE_INTERFACE, NULL);
    gBS->CloseEvent (Event);
  }
}

EFI_STATUS EFIAPI
NvStoreInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_EVENT AddressEvent, ExitEvent;
  EFI_STATUS Status;
  DriverHandle = Image;
  if (PcdGetBool (PcdFirmwareOnEmmc))
  {
    // boot0 holds only the MAC pair. General variables remain volatile.
    EfiCreateProtocolNotifyEvent (&gTh1520MacStoreProtocolGuid,
        TPL_CALLBACK, MacReady, NULL, &Registration);
    return EFI_SUCCESS;
  }
  Nv = AllocateRuntimeZeroPool (sizeof (*Nv));
  if (!Nv)
    return EFI_OUT_OF_RESOURCES;
  VOID *IoPages = AllocateRuntimePages (TH1520_SPI_RUNTIME_PAGES);
  if (!IoPages)
  {
    FreePool (Nv);
    return EFI_OUT_OF_RESOURCES;
  }
  Th1520SpiRuntimePrepare (IoPages);
  Nv->Store.Offset = TH1520_NV_SPI_OFFSET;
  Nv->Store.Read = Th1520SpiRead;
  Nv->Store.Write = Th1520SpiProgram;
  Nv->Store.Erase = Th1520SpiErase;
  Nv->Store.Scratch = Nv->Scratch;
  Nv->Status = gDS->SetMemorySpaceAttributes (0xffea000000ULL,
      EFI_PAGE_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = gDS->SetMemorySpaceAttributes (0xffef010000ULL,
        EFI_PAGE_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = Th1520SpiInitNative ();
  OriginalSet = gRT->SetVariable;
  OriginalGet = gRT->GetVariable;
  OriginalNext = gRT->GetNextVariableName;
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = Th1520NvLoad (&Nv->Store, Nv->Cache, &Nv->Length);
  if (Nv->Status == EFI_NOT_FOUND)
    Nv->Status = EFI_SUCCESS;
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = Replay ();
  if (!EFI_ERROR (Nv->Status))
    Nv->Status = Snapshot ();
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY,
      AddressChange, NULL, &gEfiEventVirtualAddressChangeGuid,
      &AddressEvent);
  if (EFI_ERROR (Status))
    Nv->Status = Status;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY,
      PrepareRuntime, NULL, &gEfiEventExitBootServicesGuid, &ExitEvent);
  if (EFI_ERROR (Status))
    Nv->Status = Status;
  EDKII_VARIABLE_LOCK_PROTOCOL *Lock;
  if (!EFI_ERROR (gBS->LocateProtocol (&gEdkiiVariableLockProtocolGuid,
                                     NULL, (VOID **)&Lock)))
    Lock->RequestToLock (Lock, L"FallbackMac", &gTh1520NvStoreGuid);
  gRT->SetVariable = SetVariable;
  gRT->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gRT, gRT->Hdr.HeaderSize, &gRT->Hdr.CRC32);
  if (gST->ConOut)
    Print (L"UEFI variables: SPI, %r\n", Nv->Status);
  return gBS->InstallProtocolInterface (&DriverHandle,
      &gTh1520NvStoreReadyProtocolGuid, EFI_NATIVE_INTERFACE,
      &Nv->Status);
}
