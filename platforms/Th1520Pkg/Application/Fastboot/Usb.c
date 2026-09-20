// SPDX-License-Identifier: BSD-2-Clause-Patent
// Polled DWC3 USB2 device transport. Every DMA object owns full cache lines.
#include "Fastboot.h"
#include <Library/Th1520BoardLib.h>
#include <Library/PcdLib.h>
#include <IndustryStandard/Usb.h>
#include <Protocol/Th1520UsbRole.h>
#include <Library/UefiLib.h>

#define USB_BASE 0xffe7040000ULL
#define GCTL 0xc110
#define USB2PHY 0xc200
#define USB3PIPE 0xc2c0
#define EVENT_LO 0xc400
#define EVENT_HI 0xc404
#define EVENT_SIZE 0xc408
#define EVENT_COUNT 0xc40c
#define DCFG 0xc700
#define DCTL 0xc704
#define DEVTEN 0xc708
#define DSTS 0xc70c
#define EP_ENABLE 0xc720
#define EP_CMD(Ep) (0xc80c + 16 * (Ep))
#define RX_SIZE 16384
#define EVENT_BYTES 4096

typedef struct
{
  UINT32 Low, High, Size, Control;
  UINT8 Padding[48];
} TRB;
typedef struct
{
  UINT32 Events[EVENT_BYTES / 4];
  TRB Trb[4];
  UINT8 Setup[64];
  UINT8 Control[512];
  UINT8 Rx[RX_SIZE];
  UINT8 Tx[RX_SIZE];
} USB_DMA;
STATIC USB_DMA *Dma;
STATIC TH1520_USB_ROLE_PROTOCOL *Role;
STATIC UINTN EventOffset, Received, RxLength;
STATIC BOOLEAN RxArmed;
STATIC BOOLEAN Configured, RxReady, TxBusy, CtrlData, CtrlStatus;
STATIC UINT8 Configuration;
STATIC UINT16 BulkPacket;
STATIC EFI_STATUS UsbError;
STATIC UINT32 Generation, SeenGeneration;
STATIC UINT8 Resource[4];

STATIC CONST UINT8 Device[] = {18, 1, 0, 2, 0, 0, 0, 64, 0xd1, 0x18, 0xe0, 0x4e, 0, 1, 1, 2, 3, 1};
STATIC UINT8 Config[] = {9, 2, 32, 0, 1,    1, 0, 0xc0, 0, 9, 4, 0, 0, 2, 0xff, 0x42,
                         3, 0, 7,  5, 0x81, 2, 0, 2,    0, 7, 5, 1, 2, 0, 2,    0};
STATIC CONST UINT8 Qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
STATIC CHAR16 Manufacturer[32], Product[64], Serial[32];
STATIC CONST CHAR16 *Strings[] = {L"", Manufacturer, Product, Serial};
STATIC UINT32
Read (UINTN Offset)
{
  return MmioRead32 (USB_BASE + Offset);
}
STATIC VOID
Write (UINTN Offset, UINT32 Value)
{
  MmioWrite32 (USB_BASE + Offset, Value);
}
STATIC EFI_STATUS
Wait (UINTN Offset, UINT32 Mask, UINT32 Value)
{
  UINTN N;
  for (N = 0; N < 100000; N++)
  {
    if ((Read (Offset) & Mask) == Value)
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}
STATIC EFI_STATUS
Command (UINTN Ep, UINT32 Cmd, UINT32 P0, UINT32 P1, UINT32 P2)
{
  EFI_STATUS Status;
  Write (EP_CMD (Ep) - 4, P0);
  Write (EP_CMD (Ep) - 8, P1);
  Write (EP_CMD (Ep) - 12, P2);
  Write (EP_CMD (Ep), Cmd | BIT10);
  Status = Wait (EP_CMD (Ep), BIT10, 0);
  if (!EFI_ERROR (Status) && (Read (EP_CMD (Ep)) & (15 << 12)))
  {
    Status = EFI_DEVICE_ERROR;
  }
  if (EFI_ERROR (Status))
  {
    UsbError = Status;
  }
  return Status;
}
STATIC VOID
Transfer (UINTN Ep, VOID *Buffer, UINT32 Length, UINT32 Type)
{
  TRB *T = &Dma->Trb[Ep];
  ZeroMem (T, sizeof (*T));
  T->Low = (UINT32)(UINTN)Buffer;
  T->High = (UINT32)((UINTN)Buffer >> 32);
  T->Size = Length;
  T->Control = BIT0 | BIT1 | BIT11 | (Type << 4);
  WriteBackInvalidateDataCacheRange (Buffer, Length);
  WriteBackInvalidateDataCacheRange (T, sizeof (*T));
  if (!EFI_ERROR (Command (Ep, 6, (UINTN)T >> 32, (UINT32)(UINTN)T, 0)))
  {
    Resource[Ep] = (Read (EP_CMD (Ep)) >> 16) & 0x7f;
  }
}
STATIC VOID
Endpoint (UINTN Ep, UINTN Packet, BOOLEAN Modify)
{
  UINT32 P0 = (UINT32)(Packet << 3);
  if (Ep >= 2)
  {
    P0 |= 2 << 1;
  }
  if (Ep & 1)
  {
    P0 |= (UINT32)((Ep >> 1) << 17);
  }
  if (Modify)
  {
    P0 |= 2U << 30;
  }
  Command (Ep, 1, P0, (UINT32)(Ep << 25) | BIT8 | BIT10, 0);
  if (!Modify)
  {
    Command (Ep, 2, 1, 0, 0);
  }
}
STATIC VOID
Setup (VOID)
{
  CtrlData = FALSE;
  CtrlStatus = FALSE;
  Transfer (0, Dma->Setup, 8, 2);
}
STATIC VOID
Stall (VOID)
{
  Command (0, 4, 0, 0, 0);
  Setup ();
}
STATIC VOID
ControlRequest (VOID)
{
  USB_DEVICE_REQUEST *R = (VOID *)Dma->Setup;
  UINTN Size = 0, Index;
  CONST VOID *Data = NULL;
  InvalidateDataCacheRange (Dma->Setup, sizeof (Dma->Setup));
  if ((R->RequestType & 0x60) != 0)
  {
    Stall ();
    return;
  }
  switch (R->Request)
  {
  case USB_REQ_GET_DESCRIPTOR:
    if (R->RequestType != 0x80)
    {
      Stall ();
      return;
    }
    switch (R->Value >> 8)
    {
    case 1:
      Data = Device;
      Size = sizeof (Device);
      break;
    case 2:
    case 7:
      Config[1] = (UINT8)(R->Value >> 8);
      Index = ((R->Value >> 8) == 7) ? (BulkPacket == 512 ? 64 : 512) : BulkPacket;
      Config[22] = Config[29] = Index & 255;
      Config[23] = Config[30] = Index >> 8;
      Data = Config;
      Size = sizeof (Config);
      break;
    case 6:
      Data = Qualifier;
      Size = sizeof (Qualifier);
      break;
    case 3:
      Index = R->Value & 255;
      if (Index >= ARRAY_SIZE (Strings))
      {
        Stall ();
        return;
      }
      if (Index == 0)
      {
        Dma->Control[2] = 9;
        Dma->Control[3] = 4;
        Size = 4;
      }
      else
      {
        Size = 2 + StrSize (Strings[Index]) - 2;
        CopyMem (Dma->Control + 2, Strings[Index], Size - 2);
      }
      Dma->Control[0] = (UINT8)Size;
      Dma->Control[1] = 3;
      Data = Dma->Control;
      break;
    default:
      Stall ();
      return;
    }
    break;
  case USB_REQ_SET_ADDRESS:
    if (R->RequestType || R->Value > 127 || R->Length)
    {
      Stall ();
      return;
    }
    Write (DCFG, (Read (DCFG) & ~(127U << 3)) | (R->Value << 3));
    break;
  case USB_REQ_SET_CONFIG:
    if (R->RequestType || R->Value > 1 || R->Length)
    {
      Stall ();
      return;
    }
    if (Configured)
    {
      Stall ();
      return;
    }
    Configuration = (UINT8)R->Value;
    if (Configuration)
    {
      Command (0, 9 | (2 << 16), 0, 0, 0);
      Endpoint (2, BulkPacket, FALSE);
      Endpoint (3, BulkPacket, FALSE);
      Write (EP_ENABLE, 15);
      Configured = TRUE;
      RxReady = FALSE;
      TxBusy = FALSE;
    }
    break;
  case USB_REQ_GET_CONFIG:
    Dma->Control[0] = Configuration;
    Data = Dma->Control;
    Size = 1;
    break;
  case USB_REQ_GET_STATUS:
    ZeroMem (Dma->Control, 2);
    Data = Dma->Control;
    Size = 2;
    break;
  case USB_REQ_GET_INTERFACE:
    Dma->Control[0] = 0;
    Data = Dma->Control;
    Size = 1;
    break;
  case USB_REQ_SET_INTERFACE:
    if (R->Value || R->Index)
    {
      Stall ();
      return;
    }
    break;
  case USB_REQ_CLEAR_FEATURE:
    if (R->RequestType != 2 || R->Value != 0 || (R->Index != 1 && R->Index != 0x81))
    {
      Stall ();
      return;
    }
    Command ((R->Index & 0x80) ? 3 : 2, 5, 0, 0, 0);
    break;
  default:
    Stall ();
    return;
  }
  if (R->Length)
  {
    if (!(R->RequestType & 0x80) || Data == NULL)
    {
      Stall ();
      return;
    }
    Size = MIN (Size, R->Length);
    if (Data != Dma->Control)
    {
      CopyMem (Dma->Control, Data, Size);
    }
    CtrlData = TRUE;
    Transfer (1, Dma->Control, (UINT32)Size, 5);
  }
  // The controller requests the status phase with XferNotReady.
  CtrlStatus = TRUE;
}
STATIC VOID
Poll (VOID)
{
  UINT32 Count, Event, Type, Ep;
  if (Dma == NULL)
  {
    return;
  }
  Count = Read (EVENT_COUNT) & 0xfffc;
  while (Count >= 4)
  {
    InvalidateDataCacheRange (&Dma->Events[EventOffset / 4], 4);
    Event = Dma->Events[EventOffset / 4];
    EventOffset = (EventOffset + 4) % EVENT_BYTES;
    Write (EVENT_COUNT, 4);
    Count -= 4;
    if (Event & 1)
    {
      Type = (Event >> 8) & 15;
      if (Type == 0 || Type == 1)
      {
        UINTN EndpointIndex;
        for (EndpointIndex = 2; EndpointIndex < 4; EndpointIndex++)
        {
          if (Resource[EndpointIndex])
          {
            Command (EndpointIndex, 8 | BIT11 | (Resource[EndpointIndex] << 16), 0, 0, 0);
          }
          Resource[EndpointIndex] = 0;
          if (Configured)
          {
            Command (EndpointIndex, 5, 0, 0, 0);
          }
        }
        Generation++;
        Configured = FALSE;
        Configuration = 0;
        RxReady = FALSE;
        RxArmed = FALSE;
        TxBusy = FALSE;
        Write (DCFG, Read (DCFG) & ~(127U << 3));
        Write (EP_ENABLE, 3);
      }
      else if (Type == 2)
      {
        BulkPacket = (Read (DSTS) & 7) == 0 ? 512 : 64;
        Endpoint (0, 64, TRUE);
        Endpoint (1, 64, TRUE);
      }
      else if (Type == 11)
      {
        UsbError = EFI_DEVICE_ERROR;
      }
      continue;
    }
    Ep = (Event >> 1) & 31;
    Type = (Event >> 6) & 15;
    if (Ep < 2)
    {
      if (Type == 1)
      {
        if (!CtrlStatus && !CtrlData)
        {
          ControlRequest ();
        }
        else if (CtrlData)
        {
          CtrlData = FALSE;
        }
        else
        {
          Setup ();
        }
      }
      else if (Type == 3 && ((Event >> 12) & 3) == 2 && CtrlStatus && !CtrlData)
      {
        Transfer (Ep, Dma->Control, 0, ((USB_DEVICE_REQUEST *)Dma->Setup)->Length ? 4 : 3);
      }
    }
    else if (Type == 1 && Ep == 2)
    {
      InvalidateDataCacheRange (&Dma->Trb[2], sizeof (TRB));
      Resource[2] = 0;
      RxArmed = FALSE;
      Received = RxLength - (Dma->Trb[2].Size & 0xffffff);
      InvalidateDataCacheRange (Dma->Rx, RX_SIZE);
      RxReady = TRUE;
    }
    else if (Type == 1 && Ep == 3)
    {
      Resource[3] = 0;
      TxBusy = FALSE;
    }
  }
}
EFI_STATUS
UsbStart (VOID)
{
  EFI_PHYSICAL_ADDRESS Address = MAX_UINT32;
  EFI_STATUS Status;
  AsciiStrToUnicodeStrS ((CONST CHAR8 *)PcdGetPtr (PcdBoardManufacturer), Manufacturer,
                         ARRAY_SIZE (Manufacturer));
  AsciiStrToUnicodeStrS ((CONST CHAR8 *)PcdGetPtr (PcdFirmwareName), Product, ARRAY_SIZE (Product));
  StrCatS (Product, ARRAY_SIZE (Product), L" Fastboot");
  AsciiStrToUnicodeStrS ((CONST CHAR8 *)PcdGetPtr (PcdFastbootProduct), Serial,
                         ARRAY_SIZE (Serial));
  Role = NULL;
  Status = gBS->LocateProtocol (&gTh1520UsbRoleProtocolGuid, NULL, (VOID **)&Role);
  if (!EFI_ERROR (Status))
  {
    Status = Role->EnterDevice (Role);
    if (EFI_ERROR (Status))
    {
      Role = NULL;
      return Status;
    }
  }
  if (Role == NULL)
  {
    Status = Th1520BoardUsbRole (TRUE);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
  }
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData,
                               EFI_SIZE_TO_PAGES (sizeof (USB_DMA)), &Address);
  if (EFI_ERROR (Status))
  {
    UsbStop ();
    return Status;
  }
  Dma = (VOID *)(UINTN)Address;
  ZeroMem (Dma, sizeof (*Dma));
  UsbError = EFI_SUCCESS;
  EventOffset = 0;
  Configured = FALSE;
  RxArmed = FALSE;
  RxReady = FALSE;
  BulkPacket = 512;
  Generation = 0;
  SeenGeneration = 0;
  ZeroMem (Resource, sizeof (Resource));
  MmioOr32 (0xfffc02d104ULL, 15);
  MmioOr32 (0xffec03f034ULL, 1);
  MicroSecondDelay (10);
  MmioAnd32 (0xffec02c014ULL, ~7U);
  MicroSecondDelay (1000);
  MmioOr32 (0xffec02c014ULL, 7);
  MmioWrite32 (0xffec03f030ULL, 0x77f);
  MmioWrite32 (0xffec03f02cULL, 0x15150f0);
  Write (DCTL, BIT30);
  Status = Wait (DCTL, BIT30, 0);
  if (EFI_ERROR (Status))
  {
    UsbStop ();
    return Status;
  }
  Write (GCTL, (Read (GCTL) & ~((3U << 12) | BIT1)) | (2 << 12));
  Write (USB2PHY, Read (USB2PHY) & ~(BIT6 | BIT8));
  Write (USB3PIPE, Read (USB3PIPE) & ~BIT17);
  WriteBackInvalidateDataCacheRange (Dma, sizeof (*Dma));
  Write (EVENT_LO, (UINT32)Address);
  Write (EVENT_HI, (UINT32)(Address >> 32));
  Write (EVENT_SIZE, EVENT_BYTES | BIT31);
  Write (EVENT_COUNT, 0);
  Write (DCFG, 0);
  Write (DEVTEN, 7 | BIT11);
  Command (0, 9, 0, 0, 0);
  Endpoint (0, 64, FALSE);
  Endpoint (1, 64, FALSE);
  Write (EP_ENABLE, 3);
  Setup ();
  Write (DCTL, Read (DCTL) | BIT31);
  return UsbError;
}
VOID
UsbStop (VOID)
{
  if (Dma == NULL)
  {
    if (Role != NULL)
    {
      EFI_STATUS Status = Role->LeaveDevice (Role);
      if (EFI_ERROR (Status))
      {
        Print (L"USB host restore failed: %r\r\n", Status);
      }
      Role = NULL;
    }
    return;
  }
  Write (DCTL, Read (DCTL) & ~BIT31);
  if (EFI_ERROR (Wait (DSTS, BIT22, BIT22)))
  {
    Write (DCTL, BIT30);
    // Do not free DMA memory if hardware could still be accessing it.
    if (EFI_ERROR (Wait (DCTL, BIT30, 0)))
    {
      return;
    }
  }
  Write (DEVTEN, 0);
  Write (EVENT_SIZE, BIT31);
  Write (EVENT_LO, 0);
  Write (EVENT_HI, 0);
  gBS->FreePages ((UINTN)Dma, EFI_SIZE_TO_PAGES (sizeof (*Dma)));
  Dma = NULL;
  if (Role != NULL)
  {
    EFI_STATUS Status = Role->LeaveDevice (Role);
    if (EFI_ERROR (Status))
    {
      Print (L"USB host restore failed: %r\r\n", Status);
    }
    Role = NULL;
  }
}
EFI_STATUS
UsbReceive (VOID *Buffer, UINTN *Length)
{
  Poll ();
  if (EFI_ERROR (UsbError))
  {
    return UsbError;
  }
  if (SeenGeneration != Generation)
  {
    SeenGeneration = Generation;
    return EFI_MEDIA_CHANGED;
  }
  if (!RxReady)
  {
    if (Configured && !RxArmed)
    {
      RxLength = ALIGN_VALUE (MIN (*Length, RX_SIZE), BulkPacket);
      Transfer (2, Dma->Rx, (UINT32)RxLength, 1);
      RxArmed = TRUE;
    }
    return EFI_NOT_READY;
  }
  if (*Length < Received)
  {
    return EFI_BUFFER_TOO_SMALL;
  }
  *Length = Received;
  CopyMem (Buffer, Dma->Rx, Received);
  RxReady = FALSE;
  return UsbError;
}
EFI_STATUS
UsbSend (CONST VOID *Buffer, UINTN Length)
{
  UINTN N;
  UINT32 Epoch = Generation;
  if (Length > sizeof (Dma->Tx) || !Configured)
  {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (Dma->Tx, Buffer, Length);
  TxBusy = TRUE;
  Transfer (3, Dma->Tx, (UINT32)Length, 1);
  for (N = 0; N < 300000; N++)
  {
    Poll ();
    if (Generation != Epoch)
    {
      return EFI_ABORTED;
    }
    if (EFI_ERROR (UsbError))
    {
      return UsbError;
    }
    if (!TxBusy)
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (100);
  }
  return EFI_TIMEOUT;
}
