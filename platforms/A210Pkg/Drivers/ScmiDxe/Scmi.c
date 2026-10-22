// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Guid/FdtHob.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/A210Scmi.h>

#define SMT_STATUS 0x04
#define SMT_FLAGS 0x10
#define SMT_LENGTH 0x14
#define SMT_HEADER 0x18
#define SMT_PAYLOAD 0x1c
#define SMT_SIZE 256
#define SMT_FREE BIT0
#define MBOX_GENERATE 0x10
#define MBOX_NOTIFY BIT6
#define TIMEOUT_US 250000

STATIC UINTN mShmem, mDoorbell;
STATIC UINT32 mToken;
STATIC BOOLEAN mFailed, mBusy, mExited;

STATIC UINT64
Cells (CONST UINT32 *Data, UINTN Count)
{
  UINT64 Value = 0;
  while (Count--)
    Value = LShiftU64 (Value, 32) | SwapBytes32 (*Data++);
  return Value;
}

// Translate a register through each bus's ranges, including the SRAM child.
STATIC EFI_STATUS
Register (CONST VOID *Fdt, INT32 Node, UINTN Index,
          UINTN *Base, UINTN *Size)
{
  CONST UINT32 *Data;
  INT32 Parent = FdtParentOffset (Fdt, Node), Length, Ac, Sc, Pac;
  UINT64 Address, Bytes;
  UINTN Width, Offset, Depth;
  Ac = FdtAddressCells (Fdt, Parent);
  Sc = FdtSizeCells (Fdt, Parent);
  if (Ac < 1 || Ac > 2 || Sc < 1 || Sc > 2)
    return EFI_UNSUPPORTED;
  Width = Ac + Sc;
  Data = FdtGetProp (Fdt, Node, "reg", &Length);
  if (!Data || Length < 0 || Length % (Width * 4)
      || Index >= (UINTN)Length / (Width * 4))
    return EFI_UNSUPPORTED;
  Address = Cells (Data + Index * Width, Ac);
  Bytes = Cells (Data + Index * Width + Ac, Sc);
  if (!Bytes || Address > MAX_UINT64 - Bytes)
    return EFI_UNSUPPORTED;
  for (Depth = 0; Parent > 0 && Depth < 8; Depth++)
  {
    INT32 Next = FdtParentOffset (Fdt, Parent);
    BOOLEAN Found = FALSE;
    Pac = FdtAddressCells (Fdt, Next);
    if (Pac < 1 || Pac > 2)
      return EFI_UNSUPPORTED;
    Data = FdtGetProp (Fdt, Parent, "ranges", &Length);
    Width = Ac + Pac + Sc;
    if (!Data || Length < 0 || Length % (Width * 4))
      return EFI_UNSUPPORTED;
    if (!Length)
      Found = TRUE;
    for (Offset = 0; Offset < (UINTN)Length / 4; Offset += Width)
    {
      UINT64 Child = Cells (Data + Offset, Ac);
      UINT64 Target = Cells (Data + Offset + Ac, Pac);
      UINT64 Span = Cells (Data + Offset + Ac + Pac, Sc);
      if (Address < Child || Bytes > Span || Address - Child > Span - Bytes)
        continue;
      if (Target > MAX_UINT64 - (Address - Child)
          || Target + Address - Child > MAX_UINT64 - Bytes)
        return EFI_UNSUPPORTED;
      Address = Target + (Address - Child);
      Found = TRUE;
      break;
    }
    if (!Found)
      return EFI_UNSUPPORTED;
    Parent = Next;
    Ac = Pac;
    Sc = FdtSizeCells (Fdt, Parent);
    if (Sc < 1 || Sc > 2)
      return EFI_UNSUPPORTED;
  }
  if (Parent != 0 || Address > MAX_UINTN - Bytes)
    return EFI_UNSUPPORTED;
  *Base = Address;
  *Size = Bytes;
  return EFI_SUCCESS;
}

STATIC BOOLEAN
Enabled (CONST VOID *Fdt, INT32 Node)
{
  for (; Node >= 0; Node = FdtParentOffset (Fdt, Node))
  {
    INT32 Length;
    CONST CHAR8 *Status = FdtGetProp (Fdt, Node, "status", &Length);
    if (Status && !((Length == 5 && !CompareMem (Status, "okay", 5))
                   || (Length == 3 && !CompareMem (Status, "ok", 3))))
      return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN
Compatible (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Length;
  CONST CHAR8 *List = FdtGetProp (Fdt, Node, "compatible", &Length);
  return List && Length > 0 && FdtStringListContains (List, Length, Name);
}

STATIC EFI_STATUS
LocateChannel (CONST VOID *Fdt)
{
  CONST UINT32 *Data;
  INT32 Scmi, Node, Length, Index;
  UINTN Base, Size;
  EFI_STATUS Status;
  if (FdtNodeOffsetByCompatible (Fdt, -1, "zhihe,a210") != 0)
    return EFI_UNSUPPORTED;
  Scmi = FdtNodeOffsetByCompatible (Fdt, -1, "arm,scmi");
  if (Scmi < 0 || !Enabled (Fdt, Scmi))
    return EFI_NOT_FOUND;
  // Use the first (tx) channel, with a single selector cell. A notification
  // channel and its shared memory may follow; EDK2 does not use them.
  if (FdtStringListSearch (Fdt, Scmi, "mbox-names", "tx") != 0)
    return EFI_UNSUPPORTED;
  Data = FdtGetProp (Fdt, Scmi, "mboxes", &Length);
  if (!Data || Length < 8 || Cells (Data + 1, 1))
    return EFI_UNSUPPORTED;
  Node = FdtNodeOffsetByPhandle (Fdt, Cells (Data, 1));
  if (Node < 0 || !Enabled (Fdt, Node)
      || !Compatible (Fdt, Node, "zhihe,a210-ap-mailbox"))
    return EFI_UNSUPPORTED;
  Index = FdtStringListSearch (Fdt, Node, "reg-names", "tx");
  if (Index < 0)
    return EFI_UNSUPPORTED;
  Status = Register (Fdt, Node, Index, &Base, &Size);
  if (EFI_ERROR (Status) || Base != 0x00311000 || Size < 0x14)
    return EFI_UNSUPPORTED;
  mDoorbell = Base + MBOX_GENERATE;
  Data = FdtGetProp (Fdt, Scmi, "shmem", &Length);
  if (!Data || Length < 4)
    return EFI_UNSUPPORTED;
  Node = FdtNodeOffsetByPhandle (Fdt, Cells (Data, 1));
  if (Node < 0 || !Enabled (Fdt, Node)
      || !Compatible (Fdt, Node, "arm,scmi-shmem"))
    return EFI_UNSUPPORTED;
  Status = Register (Fdt, Node, 0, &Base, &Size);
  if (EFI_ERROR (Status) || Base != 0x30907c00 || Size != SMT_SIZE)
    return EFI_UNSUPPORTED;
  mShmem = Base;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
WaitFree (VOID)
{
  UINTN Elapsed;
  for (Elapsed = 0; Elapsed < TIMEOUT_US; Elapsed += 10)
  {
    if (MmioRead32 (mShmem + SMT_STATUS) & SMT_FREE)
      return EFI_SUCCESS;
    MicroSecondDelay (10);
  }
  mFailed = TRUE;
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS EFIAPI
Execute (A210_SCMI_PROTOCOL *This, UINT8 Protocol, UINT8 Message,
         UINT32 *Buffer, UINT32 RequestSize, UINT32 *ResponseSize)
{
  UINT32 Header, Length;
  UINTN Index;
  EFI_TPL Tpl;
  EFI_STATUS Status;
  if (!Buffer || !ResponseSize || RequestSize > A210_SCMI_PAYLOAD_SIZE
      || (RequestSize & 3))
    return EFI_INVALID_PARAMETER;
  if (mExited || mFailed)
    return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mBusy)
  {
    gBS->RestoreTPL (Tpl);
    return EFI_NOT_READY;
  }
  mBusy = TRUE;
  Status = WaitFree ();
  if (EFI_ERROR (Status))
    goto Done;
  Header = ((mToken++ & 0x3ff) << 18) | (Protocol << 10) | Message;
  MmioWrite32 (mShmem + SMT_STATUS, 0);
  MmioWrite32 (mShmem + SMT_FLAGS, 0);
  MmioWrite32 (mShmem + SMT_LENGTH, RequestSize + 4);
  MmioWrite32 (mShmem + SMT_HEADER, Header);
  for (Index = 0; Index < RequestSize / 4; Index++)
    MmioWrite32 (mShmem + SMT_PAYLOAD + Index * 4, Buffer[Index]);
  MemoryFence ();
  MmioWrite32 (mDoorbell, MBOX_NOTIFY);
  Status = WaitFree ();
  if (EFI_ERROR (Status))
    goto Done;
  MemoryFence ();
  Length = MmioRead32 (mShmem + SMT_LENGTH);
  if (MmioRead32 (mShmem + SMT_STATUS) != SMT_FREE
      || MmioRead32 (mShmem + SMT_HEADER) != Header
      || Length < 8 || Length > SMT_SIZE - SMT_HEADER || (Length & 3))
  {
    mFailed = TRUE;
    Status = EFI_PROTOCOL_ERROR;
    goto Done;
  }
  Length -= 4;
  if (*ResponseSize < Length)
  {
    *ResponseSize = Length;
    Status = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }
  for (Index = 0; Index < Length / 4; Index++)
    Buffer[Index] = MmioRead32 (mShmem + SMT_PAYLOAD + Index * 4);
  *ResponseSize = Length;
Done:
  mBusy = FALSE;
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC A210_SCMI_PROTOCOL mProtocol = { Execute };

STATIC VOID EFIAPI
ExitBootServices (EFI_EVENT Event, VOID *Context)
{
  // Leave the free channel and the E902 firmware intact for the next stage.
  mExited = TRUE;
}

EFI_STATUS EFIAPI
A210ScmiInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  VOID *Hob = GetFirstGuidHob (&gFdtHobGuid);
  VOID *Fdt;
  UINT32 Response[2], Size = sizeof (Response);
  EFI_EVENT ExitEvent;
  EFI_STATUS Status;
  if (!Hob || GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64))
    return EFI_NOT_FOUND;
  Fdt = (VOID *)(UINTN)*(UINT64 *)GET_GUID_HOB_DATA (Hob);
  Status = LocateChannel (Fdt);
  if (EFI_ERROR (Status))
    return Status;
  Status = Execute (&mProtocol, 0x10, 0, Response, 0, &Size);
  if (EFI_ERROR (Status))
    return Status;
  if (Size != sizeof (Response) || Response[0] || (Response[1] >> 16) < 1)
    return EFI_UNSUPPORTED;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBootServices,
                              NULL, &gEfiEventExitBootServicesGuid, &ExitEvent);
  if (EFI_ERROR (Status))
    return Status;
  Status = gBS->InstallProtocolInterface (&Image, &gA210ScmiProtocolGuid,
                                          EFI_NATIVE_INTERFACE, &mProtocol);
  if (EFI_ERROR (Status))
    gBS->CloseEvent (ExitEvent);
  DEBUG ((DEBUG_INFO, "A210 SCMI: Base %x, transport %r\n", Response[1], Status));
  return Status;
}
