// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Guid/RiscVSecHobData.h>
#include <Library/A210ClockLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/MapMmioLib.h>
#include <Library/PlatformSmbiosLib.h>
#include <Library/PrintLib.h>

EFI_STATUS
PlatformGetProcessorInfo (CONST VOID *Fdt, PLATFORM_PROCESSOR_INFO *Info)
{
  INT32 Cpus, Node, Length;
  CONST CHAR8 *Value;
  UINTN C908 = 0, C920 = 0;
  EFI_STATUS Status;
  STATIC CONST EFI_GUID SecGuid = RISCV_SEC_HANDOFF_HOB_GUID;
  VOID *Hob;

  if (Fdt == NULL || Info == NULL)
    return EFI_INVALID_PARAMETER;
  if (FdtNodeOffsetByCompatible (Fdt, -1, "zhihe,a210") != 0)
    return EFI_UNSUPPORTED;
  ZeroMem (Info, sizeof (*Info));
  Cpus = FdtPathOffset (Fdt, "/cpus");
  if (Cpus < 0)
    return EFI_NOT_FOUND;
  FdtForEachSubnode (Node, Fdt, Cpus)
  {
    Value = FdtGetProp (Fdt, Node, "device_type", &Length);
    if (Value == NULL || Length != 4 || CompareMem (Value, "cpu", 4))
      continue;
    Value = FdtGetProp (Fdt, Node, "compatible", &Length);
    if (Value == NULL)
      continue;
    if (FdtStringListContains (Value, Length, "thead,c908"))
      C908++;
    else if (FdtStringListContains (Value, Length, "thead,c920"))
      C920++;
    else
      continue;
    Info->Cores++;
    Value = FdtGetProp (Fdt, Node, "status", &Length);
    if (Value == NULL
        || (Length == 5 && CompareMem (Value, "okay", 5) == 0)
        || (Length == 3 && CompareMem (Value, "ok", 3) == 0))
      Info->EnabledCores++;
  }
  if (Info->Cores == 0 || Info->Cores > 8)
    return EFI_COMPROMISED_DATA;
  AsciiStrCpyS (Info->Manufacturer, sizeof (Info->Manufacturer), "T-Head");
  AsciiSPrint (Info->Name, sizeof (Info->Name),
               "A210 (%u x C920v2 + %u x C908)", C920, C908);
  Status = MapMmioMemory (A210_CPU_CLOCK_BASE, SIZE_8KB, EFI_MEMORY_UC);
  if (EFI_ERROR (Status))
    return Status;
  Hob = GetFirstGuidHob (&SecGuid);
  if (Hob != NULL
      && GET_GUID_HOB_DATA_SIZE (Hob) >= sizeof (RISCV_SEC_HANDOFF_DATA))
  {
    CONST RISCV_SEC_HANDOFF_DATA *Sec = GET_GUID_HOB_DATA (Hob);
    Info->CurrentMhz
        = (UINT16)DivU64x32 (A210CpuClock (Sec->BootHartId), 1000000);
  }
  return EFI_SUCCESS;
}
