// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/A210ScmiLib.h>
#include <Library/BaseLib.h>
#include <Library/FdtLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/A210Scmi.h>
#include <Protocol/ArmScmiClock2Protocol.h>
#include <Protocol/ArmScmiPerformanceProtocol.h>

#define SCMI_POWER 0x11
#define SCMI_CLOCK 0x14
#define SCMI_RESET 0x16
#define SCMI_RESET_ASSERT BIT1

STATIC EFI_STATUS
Resource (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Property,
          CONST CHAR8 *CellsName, UINTN Index, INT32 *Provider, UINT32 *Id)
{
  CONST UINT32 *Data, *Cells;
  INT32 Length, Bytes;
  UINTN Offset, Count;
  Data = FdtGetProp (Fdt, Node, Property, &Length);
  if (!Data)
    return EFI_NOT_FOUND;
  if (Length < 0 || Length % 4)
    return EFI_COMPROMISED_DATA;
  for (Offset = 0; Offset < (UINTN)Length / 4; Offset += Count + 1)
  {
    *Provider = FdtNodeOffsetByPhandle (Fdt, SwapBytes32 (Data[Offset]));
    if (*Provider < 0)
      return EFI_COMPROMISED_DATA;
    Cells = FdtGetProp (Fdt, *Provider, CellsName, &Bytes);
    if (!Cells || Bytes != 4)
      return EFI_COMPROMISED_DATA;
    Count = SwapBytes32 (*Cells);
    if (Count > 1 || Count >= (UINTN)Length / 4 - Offset)
      return EFI_UNSUPPORTED;
    if (!Index--)
    {
      *Id = Count ? SwapBytes32 (Data[Offset + 1]) : 0;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

STATIC BOOLEAN
IsProtocol (CONST VOID *Fdt, INT32 Provider, UINT32 Protocol)
{
  INT32 Length, Parent = FdtParentOffset (Fdt, Provider);
  CONST UINT32 *Reg = FdtGetProp (Fdt, Provider, "reg", &Length);
  return Reg && Length == 4 && SwapBytes32 (*Reg) == Protocol
         && Parent == FdtNodeOffsetByCompatible (Fdt, -1, "arm,scmi");
}

STATIC EFI_STATUS
Command (UINT8 Protocol, UINT8 Message, CONST UINT32 *Args, UINTN Count)
{
  A210_SCMI_PROTOCOL *Transport;
  UINT32 Buffer[3], Size = sizeof (Buffer);
  EFI_STATUS Status;
  if (Count > ARRAY_SIZE (Buffer))
    return EFI_INVALID_PARAMETER;
  Status = gBS->LocateProtocol (&gA210ScmiProtocolGuid, NULL, (VOID **)&Transport);
  if (EFI_ERROR (Status))
    return Status;
  for (UINTN Index = 0; Index < Count; Index++)
    Buffer[Index] = Args[Index];
  Status = Transport->Execute (Transport, Protocol, Message, Buffer,
                               Count * sizeof (UINT32), &Size);
  if (EFI_ERROR (Status))
    return Status;
  if (Size != sizeof (UINT32))
    return EFI_PROTOCOL_ERROR;
  switch ((INT32)Buffer[0])
  {
  case 0: return EFI_SUCCESS;
  case -1: return EFI_UNSUPPORTED;
  case -2: case -5: return EFI_INVALID_PARAMETER;
  case -3: return EFI_ACCESS_DENIED;
  case -4: return EFI_NOT_FOUND;
  case -6: return EFI_NOT_READY;
  default: return EFI_DEVICE_ERROR;
  }
}

STATIC EFI_STATUS
Clock (CONST VOID *Fdt, INT32 Node, UINTN Index, UINTN Operation, UINT64 *Rate)
{
  SCMI_CLOCK2_PROTOCOL *Clock;
  UINT32 Id;
  INT32 Provider, Length;
  CONST UINT32 *Frequency;
  CONST CHAR8 *Compatible;
  EFI_TPL Tpl;
  EFI_STATUS Status = Resource (Fdt, Node, "clocks", "#clock-cells", Index,
                                &Provider, &Id);
  if (EFI_ERROR (Status))
    return Status;
  Compatible = FdtGetProp (Fdt, Provider, "compatible", &Length);
  if (Compatible && FdtStringListContains (Compatible, Length, "fixed-clock"))
  {
    Frequency = FdtGetProp (Fdt, Provider, "clock-frequency", &Length);
    if (!Frequency || Length != 4 || !SwapBytes32 (*Frequency))
      return EFI_COMPROMISED_DATA;
    if (Operation == 2)
      return EFI_UNSUPPORTED;
    if (Rate)
      *Rate = SwapBytes32 (*Frequency);
    return EFI_SUCCESS;
  }
  if (!IsProtocol (Fdt, Provider, SCMI_CLOCK))
    return EFI_UNSUPPORTED;
  Status = gBS->LocateProtocol (&gArmScmiClock2ProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status))
    return Status;
  // Protect the standard protocol's staging buffer through response decoding.
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Operation == 0)
    Status = Clock->Enable (Clock, Id, TRUE);
  else if (Operation == 1)
    Status = Clock->RateGet (Clock, Id, Rate);
  else
    Status = Clock->RateSet (Clock, Id, *Rate);
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC INT32
ClockIndex (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  return Name ? FdtStringListSearch (Fdt, Node, "clock-names", Name) : 0;
}

EFI_STATUS
A210ScmiClockEnable (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Index = ClockIndex (Fdt, Node, Name);
  return Index < 0 ? EFI_NOT_FOUND : Clock (Fdt, Node, Index, 0, NULL);
}

EFI_STATUS
A210ScmiClockGet (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT64 *Rate)
{
  INT32 Index = ClockIndex (Fdt, Node, Name);
  if (!Rate)
    return EFI_INVALID_PARAMETER;
  return Index < 0 ? EFI_NOT_FOUND : Clock (Fdt, Node, Index, 1, Rate);
}

EFI_STATUS
A210ScmiClockSet (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT64 Rate)
{
  INT32 Index = ClockIndex (Fdt, Node, Name);
  return Index < 0 ? EFI_NOT_FOUND : Clock (Fdt, Node, Index, 2, &Rate);
}

EFI_STATUS
A210ScmiClocksEnable (CONST VOID *Fdt, INT32 Node)
{
  EFI_STATUS Status;
  UINT32 Id;
  INT32 Provider;
  for (UINTN Index = 0; ; Index++)
  {
    Status = Resource (Fdt, Node, "clocks", "#clock-cells", Index, &Provider, &Id);
    if (Status == EFI_NOT_FOUND)
      return Index ? EFI_SUCCESS : Status;
    if (EFI_ERROR (Status))
      return Status;
    Status = Clock (Fdt, Node, Index, 0, NULL);
    if (EFI_ERROR (Status))
      return Status;
  }
}

STATIC EFI_STATUS
Reset (CONST VOID *Fdt, INT32 Node, UINTN Index, BOOLEAN Asserted)
{
  UINT32 Args[3] = { 0, Asserted ? SCMI_RESET_ASSERT : 0, 0 };
  INT32 Provider;
  EFI_STATUS Status = Resource (Fdt, Node, "resets", "#reset-cells", Index,
                                &Provider, &Args[0]);
  if (EFI_ERROR (Status))
    return Status;
  if (!IsProtocol (Fdt, Provider, SCMI_RESET))
    return EFI_UNSUPPORTED;
  return Command (SCMI_RESET, 4, Args, ARRAY_SIZE (Args));
}

EFI_STATUS
A210ScmiReset (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, BOOLEAN Asserted)
{
  INT32 Index = Name ? FdtStringListSearch (Fdt, Node, "reset-names", Name) : 0;
  return Index < 0 ? EFI_NOT_FOUND : Reset (Fdt, Node, Index, Asserted);
}

EFI_STATUS
A210ScmiResetsDeassert (CONST VOID *Fdt, INT32 Node)
{
  EFI_STATUS Status;
  UINT32 Id;
  INT32 Provider;
  for (UINTN Index = 0; ; Index++)
  {
    Status = Resource (Fdt, Node, "resets", "#reset-cells", Index, &Provider, &Id);
    if (Status == EFI_NOT_FOUND)
      return EFI_SUCCESS;
    if (EFI_ERROR (Status))
      return Status;
    Status = Reset (Fdt, Node, Index, FALSE);
    if (EFI_ERROR (Status))
      return Status;
  }
}

EFI_STATUS
A210ScmiPowerOn (CONST VOID *Fdt, INT32 Node)
{
  UINT32 Args[] = { 0, 0, 0 }; // Synchronous transition to the on state.
  INT32 Provider;
  EFI_STATUS Status = Resource (Fdt, Node, "power-domains", "#power-domain-cells",
                                0, &Provider, &Args[1]);
  if (Status == EFI_NOT_FOUND)
    return EFI_SUCCESS;
  if (EFI_ERROR (Status))
    return Status;
  if (!IsProtocol (Fdt, Provider, SCMI_POWER))
    return EFI_UNSUPPORTED;
  return Command (SCMI_POWER, 4, Args, ARRAY_SIZE (Args));
}

EFI_STATUS
A210ScmiCpuRate (CONST VOID *Fdt, UINTN HartId, UINT64 *Rate)
{
  SCMI_PERFORMANCE_PROTOCOL *Perf;
  SCMI_PERFORMANCE_DOMAIN_ATTRIBUTES Attr;
  CONST UINT32 *Reg;
  INT32 Node, Length, Provider, Cpus = FdtPathOffset (Fdt, "/cpus");
  UINT32 Id, Level;
  UINT64 Scaled;
  EFI_TPL Tpl;
  EFI_STATUS Status;
  if (!Rate || Cpus < 0)
    return EFI_INVALID_PARAMETER;
  FdtForEachSubnode (Node, Fdt, Cpus)
  {
    Reg = FdtGetProp (Fdt, Node, "reg", &Length);
    if (Reg && Length == 4 && SwapBytes32 (*Reg) == HartId)
      break;
  }
  if (Node < 0)
    return EFI_NOT_FOUND;
  Status = Resource (Fdt, Node, "power-domains", "#power-domain-cells", 0,
                     &Provider, &Id);
  if (EFI_ERROR (Status))
    return Status;
  if (!IsProtocol (Fdt, Provider, 0x13))
    return EFI_UNSUPPORTED;
  Status = gBS->LocateProtocol (&gArmScmiPerformanceProtocolGuid, NULL,
                                (VOID **)&Perf);
  if (EFI_ERROR (Status))
    return Status;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = Perf->GetDomainAttributes (Perf, Id, &Attr);
  if (!EFI_ERROR (Status))
    Status = Perf->LevelGet (Perf, Id, &Level);
  gBS->RestoreTPL (Tpl);
  if (EFI_ERROR (Status))
    return Status;
  if (!Attr.SustainedPerfLevel || !Attr.SustainedFreq)
    return EFI_UNSUPPORTED;
  Scaled = MultU64x32 (Level, Attr.SustainedFreq);
  if (Scaled > MAX_UINT64 / 1000)
    return EFI_UNSUPPORTED;
  *Rate = DivU64x32 (MultU64x32 (Scaled, 1000), Attr.SustainedPerfLevel);
  return EFI_SUCCESS;
}
