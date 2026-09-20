// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseRiscVSbiLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Guid/EventGroup.h>
STATIC VOID *Watchdog = (VOID *)0xffefc30000ULL;
STATIC VOID *Clock = (VOID *)0xffef010000ULL;
STATIC VOID *ResetControl = (VOID *)0xfffff44000ULL;
STATIC VOID *Local = (VOID *)0xffffc39000ULL;
STATIC VOID *Remote = (VOID *)0xffffc44000ULL;
STATIC VOID EFIAPI
AddressChange (EFI_EVENT Event, VOID *Context)
{
  EfiConvertPointer (0, &Watchdog);
  EfiConvertPointer (0, &Clock);
  EfiConvertPointer (0, &ResetControl);
  EfiConvertPointer (0, &Local);
  EfiConvertPointer (0, &Remote);
}
EFI_STATUS EFIAPI
ResetInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_EVENT Event;
  EFI_STATUS Status;
  VOID *Regions[] = {Watchdog, Clock, ResetControl, Local, Remote};
  UINTN Index;
  for (Index = 0; Index < ARRAY_SIZE (Regions); Index++)
  {
    Status = gDS->SetMemorySpaceAttributes ((UINTN)Regions[Index], EFI_PAGE_SIZE,
                                            EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
  }
  return gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, AddressChange, NULL,
                             &gEfiEventVirtualAddressChangeGuid, &Event);
}
// AON RPC v2, watchdog service: function 5 restarts, function 7 powers off.
// The E902 transport polls INFO7 for acknowledgement of its response.
STATIC VOID
AonReset (UINT8 Function)
{
  UINTN Index;
  MmioOr32 ((UINTN)Clock + 0x208, BIT7 | BIT6 | BIT3);
  MmioAnd32 ((UINTN)Local + 0x10, ~0xffU);
  MmioAnd32 ((UINTN)Remote + 0x10, ~0xffU);
  MmioWrite32 ((UINTN)Local + 0x14, 0);
  MmioWrite32 ((UINTN)Local + 0x30, 0);
  MmioWrite32 ((UINTN)Remote + 0x30, 0);
  for (Index = 1; Index < 7; Index++)
  {
    MmioWrite32 ((UINTN)Remote + 0x14 + Index * 4, 0);
  }
  MmioWrite32 ((UINTN)Remote + 0x14, ((UINT32)Function << 24) | 0x00050702);
  MmioOr32 ((UINTN)Remote + 0x10, BIT6);
  for (Index = 0; Index < 10000; Index++)
  {
    if (MmioRead32 ((UINTN)Local + 0x14))
    {
      MmioWrite32 ((UINTN)Local + 0x14, 0);
      MmioWrite32 ((UINTN)Remote + 0x30, 0xdeadbeaf);
    }
    MicroSecondDelay (100);
  }
}
VOID EFIAPI
ResetCold (VOID)
{
  // Linux reaches this service after switching to its own address space.
  // Prefer M-mode SRST, which owns the SoC reset without runtime MMIO or
  // sharing the AON mailbox with the operating system.
  SbiSystemReset (SBI_SRST_RESET_TYPE_COLD_REBOOT, SBI_SRST_RESET_REASON_NONE);
  AonReset (5);
  // RAM recovery can run without AON firmware; retain a WDT0 fallback.
  DisableInterrupts ();
  MmioOr32 ((UINTN)Clock + 0x208, BIT3);
  MmioOr32 ((UINTN)ResetControl + 0x140, BIT8);
  MmioWrite32 ((UINTN)Watchdog + 4, 0);
  MmioWrite32 ((UINTN)Watchdog, 1);
  MmioWrite32 ((UINTN)Watchdog + 12, 0x76);
  CpuDeadLoop ();
}
VOID EFIAPI
ResetWarm (VOID)
{
  ResetCold ();
}
VOID EFIAPI
ResetShutdown (VOID)
{
  SbiSystemReset (SBI_SRST_RESET_TYPE_SHUTDOWN, SBI_SRST_RESET_REASON_NONE);
  AonReset (7);
}
VOID EFIAPI
ResetPlatformSpecific (UINTN DataSize, VOID *ResetData)
{
  ResetCold ();
}
VOID EFIAPI
ResetSystem (EFI_RESET_TYPE Type, EFI_STATUS Status, UINTN Size, VOID *Data)
{
  if (Type == EfiResetShutdown)
  {
    ResetShutdown ();
  }
  else
  {
    ResetCold ();
  }
}
