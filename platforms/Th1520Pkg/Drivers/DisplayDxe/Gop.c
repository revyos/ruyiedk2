// SPDX-License-Identifier: GPL-2.0-only
#include "Display.h"
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/FdtClient.h>
#include <Protocol/GraphicsOutput.h>

typedef struct
{
  DISPLAY_HW Hw;
  EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
  EFI_EDID_DISCOVERED_PROTOCOL Discovered;
  EFI_EDID_ACTIVE_PROTOCOL Active;
  FRAME_BUFFER_CONFIGURE *Blt;
  UINTN BltSize;
  EFI_PHYSICAL_ADDRESS Buffer;
  UINTN BufferSize;
  EFI_EVENT Refresh, Exit;
  BOOLEAN Exited;
} DISPLAY;

STATIC DISPLAY mDisplay;

STATIC VOID EFIAPI Log (CONST CHAR8 *Format, ...)
{
  CHAR8 Text[256];
  VA_LIST Args;
  UINTN Size;
  VA_START (Args, Format);
  Size = AsciiVSPrint (Text, sizeof (Text), Format, Args);
  VA_END (Args);
  SerialPortWrite ((UINT8 *)Text, Size);
}
#pragma pack(1)
// This GOP is installed directly on a static controller handle. An ADR
// suffix would make BDS truncate the path and seek a separate parent.
STATIC struct
{
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {{{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
            {0x3f8a194b, 0x15ce, 0x4088, {0x86, 0xd1, 0x4c, 0x70, 0x7c, 0x32, 0x21, 0x2a}}},
           {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}};
#pragma pack()

STATIC VOID ModeInfo (CONST DISPLAY_TIMING *T, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info)
{
  ZeroMem (Info, sizeof (*Info));
  Info->HorizontalResolution = T->Width;
  Info->VerticalResolution = T->Height;
  Info->PixelsPerScanLine = T->Width;
  Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
}

STATIC EFI_STATUS EFIAPI QueryMode (EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Number,
                                    UINTN *Size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  if (This == NULL || Size == NULL || Info == NULL || Number >= mDisplay.Hw.ModeCount)
  {
    return EFI_INVALID_PARAMETER;
  }
  *Info = AllocateZeroPool (sizeof (**Info));
  if (*Info == NULL)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  ModeInfo (mDisplay.Hw.Modes[Number], *Info);
  *Size = sizeof (**Info);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI SetMode (EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Number)
{
  DISPLAY *D = &mDisplay;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
  EFI_STATUS Status;
  EFI_TPL OldTpl;
  if (This == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Number >= D->Hw.ModeCount || D->Exited)
  {
    return EFI_UNSUPPORTED;
  }
  ModeInfo (D->Hw.Modes[Number], &Info);
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  ZeroMem ((VOID *)(UINTN)D->Buffer, D->BufferSize);
  WriteBackDataCacheRange ((VOID *)(UINTN)D->Buffer, D->BufferSize);
  Status = DisplaySetTiming (&D->Hw, D->Hw.Modes[Number], D->Buffer);
  if (!EFI_ERROR (Status))
  {
    Status = FrameBufferBltConfigure ((VOID *)(UINTN)D->Buffer, &Info, D->Blt, &D->BltSize);
  }
  if (!EFI_ERROR (Status))
  {
    D->Info = Info;
    D->Mode.Mode = Number;
    D->Mode.FrameBufferSize = Info.PixelsPerScanLine * Info.VerticalResolution * 4;
  }
  else if (D->Mode.Mode != MAX_UINT32)
  {
    // Restore the last working mode if a later PHY reconfiguration fails.
    DisplaySetTiming (&D->Hw, D->Hw.Modes[D->Mode.Mode], D->Buffer);
  }
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC EFI_STATUS EFIAPI Blt (EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
                              EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,
                              EFI_GRAPHICS_OUTPUT_BLT_OPERATION Operation, UINTN Sx, UINTN Sy,
                              UINTN Dx, UINTN Dy, UINTN Width, UINTN Height, UINTN Delta)
{
  DISPLAY *D = &mDisplay;
  EFI_STATUS Status;
  EFI_TPL OldTpl;
  UINTN Offset, Bytes, X, Y;
  if (This == NULL || D->Exited || Operation >= EfiGraphicsOutputBltOperationMax ||
      Width == 0 || Height == 0 || (Buffer == NULL && Operation != EfiBltVideoToVideo))
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Width > D->Info.HorizontalResolution || Height > D->Info.VerticalResolution)
  {
    return EFI_INVALID_PARAMETER;
  }
  if ((Operation == EfiBltVideoToVideo || Operation == EfiBltVideoToBltBuffer) &&
      (Sx > D->Info.HorizontalResolution - Width || Sy > D->Info.VerticalResolution - Height))
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Operation != EfiBltVideoToBltBuffer &&
      (Dx > D->Info.HorizontalResolution - Width || Dy > D->Info.VerticalResolution - Height))
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Operation == EfiBltBufferToVideo || Operation == EfiBltVideoToBltBuffer)
  {
    X = Operation == EfiBltBufferToVideo ? Sx : Dx;
    Y = Operation == EfiBltBufferToVideo ? Sy : Dy;
    if (Delta == 0)
    {
      Delta = Width * 4;
    }
    if ((Delta & 3) != 0 || X > MAX_UINTN / 4 - Width || Delta < (X + Width) * 4 ||
        Y > MAX_UINTN - Height || Y + Height > MAX_UINTN / Delta ||
        (UINTN)Buffer > MAX_UINTN - (Y + Height) * Delta)
    {
      return EFI_INVALID_PARAMETER;
    }
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = FrameBufferBlt (D->Blt, Buffer, Operation, Sx, Sy, Dx, Dy, Width, Height, Delta);
  if (!EFI_ERROR (Status) && Operation != EfiBltVideoToBltBuffer)
  {
    Offset = (Dy * D->Info.PixelsPerScanLine + Dx) * 4;
    Bytes = ((Height - 1) * D->Info.PixelsPerScanLine + Width) * 4;
    WriteBackDataCacheRange ((VOID *)(UINTN)(D->Buffer + Offset), Bytes);
  }
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC VOID EFIAPI Refresh (EFI_EVENT Event, VOID *Context)
{
  DISPLAY *D = Context;
  // GOP clients may write FrameBufferBase without calling Blt(). Bare-mode
  // C910 DDR is cacheable, so also publish such writes while boot services run.
  if (!D->Exited && D->Mode.Mode != MAX_UINT32)
  {
    WriteBackDataCacheRange ((VOID *)(UINTN)D->Buffer, D->Mode.FrameBufferSize);
  }
}

STATIC VOID EFIAPI Exit (EFI_EVENT Event, VOID *Context)
{
  DISPLAY *D = Context;
  D->Exited = TRUE;
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)D->Buffer, D->BufferSize);
  // Leave scanout active and its reserved pages intact. The OS owns display
  // clocks and must map the framebuffer with non-cacheable/write-combine PTEs.
}

STATIC EFI_STATUS FindDevice (FDT_CLIENT_PROTOCOL *Fdt, CONST CHAR8 *Compatible, UINTN Expected,
                              UINTN *Base)
{
  INT32 Node;
  CONST VOID *Property;
  UINT32 Size;
  EFI_STATUS Status;
  Status = Fdt->FindCompatibleNode (Fdt, Compatible, &Node);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "status", &Property, &Size);
  if (!EFI_ERROR (Status) && !((Size == 5 && CompareMem (Property, "okay", 5) == 0) ||
                               (Size == 3 && CompareMem (Property, "ok", 3) == 0)))
  {
    return EFI_NOT_FOUND;
  }
  Status = Fdt->GetNodeProperty (Fdt, Node, "reg", &Property, &Size);
  if (EFI_ERROR (Status) || Size != 16 || SwapBytes64 (ReadUnaligned64 (Property)) != Expected)
  {
    return EFI_UNSUPPORTED;
  }
  *Base = Expected;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI DisplayInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  DISPLAY *D = &mDisplay;
  FDT_CLIENT_PROTOCOL *Fdt;
  EFI_HANDLE Handle = NULL;
  EFI_STATUS Status;
  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&Fdt);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = FindDevice (Fdt, "thead,th1520-dc8200", 0xffef600000ULL, &D->Hw.Dc);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = FindDevice (Fdt, "thead,th1520-dw-hdmi", 0xffef540000ULL, &D->Hw.Hdmi);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = DisplayPrepare (&D->Hw);
  if (EFI_ERROR (Status))
  {
    Log ("HDMI: controller/connection not ready: %r\r\n", Status);
    return Status;
  }
  Status = DisplayReadEdid (&D->Hw);
  if (EFI_ERROR (Status))
  {
    Log ("HDMI: EDID read: %r\r\n", Status);
  }
  DisplayParseEdid (&D->Hw);
  if (D->Hw.ModeCount == 0)
  {
    Log ("HDMI: no supported EDID mode; keeping serial console\r\n");
    return EFI_UNSUPPORTED;
  }
  D->BufferSize = EFI_PAGES_TO_SIZE (EFI_SIZE_TO_PAGES (1920 * 1080 * 4));
  D->Buffer = MAX_UINT32;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiReservedMemoryType,
                               EFI_SIZE_TO_PAGES (D->BufferSize), &D->Buffer);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  ModeInfo (&DisplayTimings[0], &D->Info);
  Status = FrameBufferBltConfigure ((VOID *)(UINTN)D->Buffer, &D->Info, NULL, &D->BltSize);
  if (Status != RETURN_BUFFER_TOO_SMALL)
  {
    goto Fail;
  }
  D->Blt = AllocatePool (D->BltSize);
  if (D->Blt == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Fail;
  }
  D->Mode.MaxMode = D->Hw.ModeCount;
  D->Mode.Mode = MAX_UINT32;
  D->Mode.Info = &D->Info;
  D->Mode.SizeOfInfo = sizeof (D->Info);
  D->Mode.FrameBufferBase = D->Buffer;
  D->Gop.QueryMode = QueryMode;
  D->Gop.SetMode = SetMode;
  D->Gop.Blt = Blt;
  D->Gop.Mode = &D->Mode;
  Status = SetMode (&D->Gop, 0);
  if (EFI_ERROR (Status))
  {
    goto Fail;
  }
  Status =
      gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Refresh, D, &D->Refresh);
  if (EFI_ERROR (Status))
  {
    goto Fail;
  }
  Status = gBS->SetTimer (D->Refresh, TimerPeriodic, 1000000); // 100 ms.
  if (EFI_ERROR (Status))
  {
    goto Fail;
  }
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Exit, D,
                               &gEfiEventExitBootServicesGuid, &D->Exit);
  if (EFI_ERROR (Status))
  {
    goto Fail;
  }
  D->Discovered.SizeOfEdid = D->Hw.EdidSize;
  D->Discovered.Edid = D->Hw.Edid;
  D->Active.SizeOfEdid = D->Hw.EdidSize;
  D->Active.Edid = D->Hw.Edid;
  Status = gBS->InstallMultipleProtocolInterfaces (
      &Handle, &gEfiDevicePathProtocolGuid, &mPath, &gEfiGraphicsOutputProtocolGuid, &D->Gop,
      &gEfiEdidDiscoveredProtocolGuid, &D->Discovered, &gEfiEdidActiveProtocolGuid, &D->Active,
      NULL);
  if (!EFI_ERROR (Status))
  {
    Log ("HDMI: DC8200 GOP %ux%u, framebuffer 0x%lx\r\n", D->Info.HorizontalResolution,
         D->Info.VerticalResolution, D->Buffer);
    return EFI_SUCCESS;
  }
Fail:
  DisplayDisable (&D->Hw);
  if (D->Refresh != NULL)
  {
    gBS->CloseEvent (D->Refresh);
  }
  if (D->Exit != NULL)
  {
    gBS->CloseEvent (D->Exit);
  }
  if (D->Blt != NULL)
  {
    FreePool (D->Blt);
  }
  gBS->FreePages (D->Buffer, EFI_SIZE_TO_PAGES (D->BufferSize));
  Log ("HDMI: GOP initialization failed: %r\r\n", Status);
  return Status;
}
