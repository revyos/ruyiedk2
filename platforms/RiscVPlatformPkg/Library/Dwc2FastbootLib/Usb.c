// SPDX-License-Identifier: BSD-2-Clause-Patent
// Polled, programmed-I/O DWC2 transport; no DMA buffers or IOMMU setup.
#include <Uefi.h>
#include <IndustryStandard/Usb.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FastbootPlatformLib.h>
#include <Library/FastbootTransportLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UsbDevicePlatformLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "Dwc2.h"

STATIC UINTN mBase;
STATIC BOOLEAN mStarted, mDedicated, mConfigured;
STATIC UINT16 mBulkPacket;
STATIC UINT32 mGeneration, mSeenGeneration;
STATIC EFI_STATUS mError;
STATIC UINT8 mSetup[8], mControl[256], mRx[TRANSFER_BYTES];
STATIC UINTN mRxBytes, mRxLimit;
STATIC BOOLEAN mRxArmed, mRxReady;
STATIC CONST UINT8 *mTxBuffer[2];
STATIC UINTN mTxBytes[2], mTxOffset[2];
STATIC BOOLEAN mTxBusy[2];
STATIC UINTN mControlBytes, mControlOffset;
STATIC BOOLEAN mControlData, mControlZlp;
STATIC UINT8 mConfiguration;

STATIC CONST UINT8 mDevice[] = { 18,   1,    0,    2, 0, 0, 0, 64, 0xd1,
                                 0x18, 0xe0, 0x4e, 0, 1, 1, 2, 3,  1 };
STATIC UINT8 mConfig[]
    = { 9, 2, 32, 0, 1,    1, 0, 0xc0, 0, 9, 4, 0,    0, 2, 0xff, 0x42,
        3, 2, 7,  5, 0x81, 2, 0, 2,    0, 7, 5, 0x02, 2, 0, 2,    0 };
STATIC CONST UINT8 mQualifier[] = { 10, 6, 0, 2, 0, 0, 0, 64, 1, 0 };

STATIC UINT32
Read (UINTN Reg)
{
  return MmioRead32 (mBase + Reg);
}

STATIC VOID
Write (UINTN Reg, UINT32 Value)
{
  MmioWrite32 (mBase + Reg, Value);
}

STATIC EFI_STATUS
Wait (UINTN Reg, UINT32 Mask, UINT32 Value)
{
  for (UINTN I = 0; I < 100000; I++)
  {
    if ((Read (Reg) & Mask) == Value)
      return EFI_SUCCESS;
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}

STATIC VOID
FillTx (UINTN Ep)
{
  if (!mTxBusy[Ep] || mTxOffset[Ep] == mTxBytes[Ep])
    return;
  UINTN Packet = Ep ? mBulkPacket : EP0_PACKET;
  UINTN Fifo = Ep; // FIFO access ports are indexed by endpoint.
  UINT32 Available = Read (mDedicated ? DTXFSTS (Ep) : GNPTXSTS);
  if (!mDedicated && ((Available >> 16) & 0xff) == 0)
    return;
  UINTN Space = (Available & 0xffff) * sizeof (UINT32);
  while (mTxOffset[Ep] < mTxBytes[Ep])
  {
    UINTN Bytes = MIN (Packet, mTxBytes[Ep] - mTxOffset[Ep]);
    UINTN Words = (Bytes + 3) / 4;
    if (Space < Words * 4)
      break;
    for (UINTN I = 0; I < Words; I++)
    {
      UINT32 Word = 0;
      UINTN N = MIN ((UINTN)4, Bytes - I * 4);
      CopyMem (&Word, mTxBuffer[Ep] + mTxOffset[Ep] + I * 4, N);
      Write (FIFO (Fifo), Word);
    }
    mTxOffset[Ep] += Bytes;
    Space -= Words * 4;
  }
}

STATIC VOID
StartIn (UINTN Ep, CONST VOID *Buffer, UINTN Bytes)
{
  UINTN Packet = Ep ? mBulkPacket : EP0_PACKET;
  mTxBuffer[Ep] = Buffer;
  mTxBytes[Ep] = Bytes;
  mTxOffset[Ep] = 0;
  mTxBusy[Ep] = TRUE;
  Write (DIEPINT (Ep), MAX_UINT32);
  Write (DIEPTSIZ (Ep),
         (UINT32)Bytes
             | ((UINT32)MAX ((Bytes + Packet - 1) / Packet, 1)
                << XFER_PACKET_SHIFT));
  Write (DIEPCTL (Ep),
         (Read (DIEPCTL (Ep)) & ~EP_SET_NAK) | EP_CLEAR_NAK | EP_ENABLE);
  FillTx (Ep);
}

STATIC VOID
ArmControlOut (VOID)
{
  Write (DOEPTSIZ (0),
         SETUP_COUNT | (1U << XFER_PACKET_SHIFT) | EP0_PACKET);
  Write (DOEPCTL (0),
         (Read (DOEPCTL (0)) & ~EP_SET_NAK) | EP_ENABLE | EP_CLEAR_NAK);
}

STATIC VOID
ControlNext (VOID)
{
  UINTN Bytes = MIN (EP0_PACKET, mControlBytes - mControlOffset);
  StartIn (0, mControl + mControlOffset, Bytes);
  mControlOffset += Bytes;
}

STATIC VOID
ControlReply (CONST VOID *Buffer, UINTN Bytes, UINTN Requested)
{
  mControlBytes = MIN (Bytes, Requested);
  mControlOffset = 0;
  mControlData = TRUE;
  mControlZlp = mControlBytes != 0 && mControlBytes < Requested
                && mControlBytes % EP0_PACKET == 0;
  if (mControlBytes && Buffer != mControl)
    CopyMem (mControl, Buffer, mControlBytes);
  ControlNext ();
}

STATIC VOID
ControlStatus (VOID)
{
  mControlData = FALSE;
  StartIn (0, mControl, 0);
}

STATIC VOID
SetConfiguration (UINT8 Value)
{
  mGeneration++;
  mTxBusy[BULK_IN] = FALSE;
  mConfiguration = Value;
  mConfigured = Value != 0;
  mRxArmed = mRxReady = FALSE;
  Write (DIEPCTL (BULK_IN), EP_SET_NAK);
  Write (DOEPCTL (BULK_OUT), EP_SET_NAK);
  if (Value == 0)
    return;
  UINT32 In
      = mBulkPacket | EP_ACTIVE | EP_TYPE_BULK | EP_DATA0 | EP_SET_NAK;
  if (mDedicated)
    In |= BULK_IN << EP_FIFO_SHIFT;
  Write (DIEPCTL (BULK_IN), In);
  Write (DOEPCTL (BULK_OUT),
         mBulkPacket | EP_ACTIVE | EP_TYPE_BULK | EP_DATA0 | EP_SET_NAK);
  if (!mDedicated)
    Write (DIEPCTL (0), (Read (DIEPCTL (0)) & ~(15U << EP_NEXT_SHIFT))
                            | (BULK_IN << EP_NEXT_SHIFT));
}

STATIC VOID
HandleSetup (VOID)
{
  UINT8 Type = mSetup[0], Request = mSetup[1];
  UINT16 Value = ReadUnaligned16 ((UINT16 *)(mSetup + 2));
  UINT16 Index = ReadUnaligned16 ((UINT16 *)(mSetup + 4));
  UINT16 Length = ReadUnaligned16 ((UINT16 *)(mSetup + 6));

  mControlData = mControlZlp = FALSE;
  Write (DIEPCTL (0), Read (DIEPCTL (0)) & ~EP_STALL);
  Write (DOEPCTL (0), Read (DOEPCTL (0)) & ~EP_STALL);
  if (Type == 0x80 && Request == USB_REQ_GET_DESCRIPTOR)
  {
    switch (Value >> 8)
    {
    case USB_DESC_TYPE_DEVICE:
      if ((Value & 0xff) == 0)
      {
        ControlReply (mDevice, sizeof (mDevice), Length);
        return;
      }
      break;
    case USB_DESC_TYPE_CONFIG:
    case 7: // Other-speed configuration.
      if ((Value & 0xff) == 0)
      {
        CopyMem (mControl, mConfig, sizeof (mConfig));
        mControl[1] = Value >> 8;
        UINT16 Packet = (Value >> 8) == 7 ? (mBulkPacket == 512 ? 64 : 512)
                                          : mBulkPacket;
        WriteUnaligned16 ((UINT16 *)(mControl + 22), Packet);
        WriteUnaligned16 ((UINT16 *)(mControl + 29), Packet);
        ControlReply (mControl, sizeof (mConfig), Length);
        return;
      }
      break;
    case USB_DESC_TYPE_STRING:
    {
      CONST CHAR16 *String = NULL;
      CHAR16 Serial[64];
      CHAR8 Ascii[64];
      UINTN Id = Value & 0xff;
      if (Id == 0)
      {
        STATIC CONST UINT8 Languages[] = { 4, 3, 9, 4 };
        ControlReply (Languages, sizeof (Languages), Length);
        return;
      }
      if (Id == 1)
        String = PcdGetPtr (PcdFirmwareVendor);
      if (Id == 2)
        String = PcdGetPtr (PcdFirmwareVersionString);
      if (Id == 3
          && !EFI_ERROR (
              FastbootGetVar ("serialno", Ascii, sizeof (Ascii))))
      {
        AsciiStrToUnicodeStrS (Ascii, Serial, ARRAY_SIZE (Serial));
        String = Serial;
      }
      if (String != NULL)
      {
        UINTN Bytes = MIN (StrLen (String), 63) * sizeof (CHAR16);
        mControl[0] = (UINT8)(Bytes + 2);
        mControl[1] = USB_DESC_TYPE_STRING;
        CopyMem (mControl + 2, String, Bytes);
        ControlReply (mControl, Bytes + 2, Length);
        return;
      }
      break;
    }
    case 6: // Device qualifier.
      ControlReply (mQualifier, sizeof (mQualifier), Length);
      return;
    }
  }
  else if (Type == 0 && Request == USB_REQ_SET_ADDRESS && Value <= 127
           && Index == 0 && Length == 0)
  {
    // DWC2 latches the new address as the status transaction completes.
    Write (DCFG,
           (Read (DCFG) & ~DCFG_ADDRESS_MASK) | ((UINT32)Value << 4));
    ControlStatus ();
    return;
  }
  else if (Type == 0 && Request == USB_REQ_SET_CONFIG && Value <= 1
           && Index == 0 && Length == 0)
  {
    SetConfiguration ((UINT8)Value);
    ControlStatus ();
    return;
  }
  else if (Type == 0x80 && Request == USB_REQ_GET_CONFIG && Length == 1)
  {
    ControlReply (&mConfiguration, 1, Length);
    return;
  }
  else if ((Type == 0x80 || Type == 0x81 || Type == 0x82)
           && Request == USB_REQ_GET_STATUS && Value == 0 && Length == 2)
  {
    UINT16 Status = Type == 0x80 ? 1 : 0; // Self-powered.
    ControlReply (&Status, sizeof (Status), Length);
    return;
  }
  else if (Type == 0x01 && Request == USB_REQ_SET_INTERFACE && Value == 0
           && Index == 0 && Length == 0)
  {
    ControlStatus ();
    return;
  }
  else if (Type == 0x81 && Request == USB_REQ_GET_INTERFACE && Index == 0
           && Length == 1)
  {
    mControl[0] = 0;
    ControlReply (mControl, 1, Length);
    return;
  }
  else if (Type == 0x02 && Request == USB_REQ_CLEAR_FEATURE && Value == 0
           && Length == 0 && (Index == 0x81 || Index == 2))
  {
    UINTN Reg = Index == 0x81 ? DIEPCTL (BULK_IN) : DOEPCTL (BULK_OUT);
    Write (Reg, (Read (Reg) & ~EP_STALL) | EP_DATA0);
    ControlStatus ();
    return;
  }
  Write (DIEPCTL (0), Read (DIEPCTL (0)) | EP_STALL);
  Write (DOEPCTL (0), Read (DOEPCTL (0)) | EP_STALL);
  ArmControlOut ();
}

STATIC VOID
ResetDevice (VOID)
{
  mConfigured = FALSE;
  mConfiguration = 0;
  mRxArmed = mRxReady = FALSE;
  mTxBusy[0] = mTxBusy[1] = FALSE;
  mControlData = mControlZlp = FALSE;
  mGeneration++;
  Write (DCFG, Read (DCFG) & ~DCFG_ADDRESS_MASK);
  Write (GRSTCTL, TX_FIFO_FLUSH | ALL_TX_FIFOS);
  if (EFI_ERROR (Wait (GRSTCTL, TX_FIFO_FLUSH, 0)))
    mError = EFI_TIMEOUT;
  Write (GRSTCTL, RX_FIFO_FLUSH);
  if (EFI_ERROR (Wait (GRSTCTL, RX_FIFO_FLUSH, 0)))
    mError = EFI_TIMEOUT;
  for (UINTN Ep = 0; Ep <= BULK_OUT; Ep++)
  {
    Write (DIEPINT (Ep), MAX_UINT32);
    Write (DOEPINT (Ep), MAX_UINT32);
    Write (DIEPCTL (Ep), EP_SET_NAK);
    Write (DOEPCTL (Ep), EP_SET_NAK);
  }
  Write (DIEPMSK, EP_TRANSFER_DONE | EP_AHB_ERROR);
  Write (DOEPMSK, EP_TRANSFER_DONE | EP_AHB_ERROR | EP_SETUP_DONE);
  Write (DAINTMSK,
         BIT0 | (1U << BULK_IN) | BIT16 | (1U << (16 + BULK_OUT)));
  Write (DIEPCTL (0), EP_ACTIVE | EP_SET_NAK);
  Write (DOEPCTL (0), EP_ACTIVE | EP_SET_NAK);
  ArmControlOut ();
}

STATIC VOID
DrainRx (VOID)
{
  for (UINTN Packet = 0; Packet < 128 && (Read (GINTSTS) & IRQ_RX_FIFO);
       Packet++)
  {
    UINT32 Rx = Read (GRXSTSP);
    UINTN Ep = Rx & 15;
    UINTN Count = (Rx >> RX_COUNT_SHIFT) & RX_COUNT_MASK;
    UINTN Type = (Rx >> RX_STATUS_SHIFT) & RX_STATUS_MASK;
    UINT8 *Dest = NULL;
    if (Type == RX_PACKET_SETUP && Ep == 0 && Count == 8)
      Dest = mSetup;
    else if (Type == RX_PACKET_DATA && Ep == BULK_OUT && mRxArmed)
    {
      if (Count <= mRxLimit - mRxBytes)
        Dest = mRx + mRxBytes;
      else
        mError = EFI_BAD_BUFFER_SIZE;
    }
    for (UINTN I = 0; I < (Count + 3) / 4; I++)
    {
      UINT32 Word = Read (FIFO (0));
      if (Dest != NULL)
        CopyMem (Dest + I * 4, &Word, MIN (4, Count - I * 4));
    }
    if (Dest != NULL && Type == RX_PACKET_DATA)
      mRxBytes += Count;
  }
}

STATIC VOID
Poll (VOID)
{
  UINT32 Status = Read (GINTSTS);
  if (Status & IRQ_USB_RESET)
  {
    Write (GINTSTS, IRQ_USB_RESET);
    ResetDevice ();
  }
  if (Status & IRQ_DISCONNECT)
  {
    Write (GINTSTS, IRQ_DISCONNECT);
    mConfigured = FALSE;
    mRxArmed = mRxReady = FALSE;
    mTxBusy[0] = mTxBusy[1] = FALSE;
    mGeneration++;
  }
  if (Status & IRQ_ENUM_DONE)
  {
    Write (GINTSTS, IRQ_ENUM_DONE);
    mBulkPacket
        = ((Read (DSTS) >> DSTS_SPEED_SHIFT) & DSTS_SPEED_MASK) == 0 ? 512
                                                                     : 64;
    Write (DCTL, Read (DCTL) | CLEAR_GLOBAL_IN_NAK | CLEAR_GLOBAL_OUT_NAK);
  }
  DrainRx ();
  UINT32 Pending = Read (DAINT);
  for (UINTN Ep = 0; Ep <= BULK_OUT; Ep++)
  {
    if (Pending & (1U << (16 + Ep)))
    {
      UINT32 Interrupt = Read (DOEPINT (Ep));
      Write (DOEPINT (Ep), Interrupt);
      if (Interrupt & EP_AHB_ERROR)
        mError = EFI_DEVICE_ERROR;
      if (Ep == 0 && (Interrupt & EP_SETUP_DONE))
        HandleSetup ();
      else if (Ep == 0 && (Interrupt & EP_TRANSFER_DONE))
        ArmControlOut ();
      else if (Ep == BULK_OUT && (Interrupt & EP_TRANSFER_DONE))
      {
        mRxArmed = FALSE;
        mRxReady = TRUE;
      }
    }
    if (Ep <= BULK_IN && (Pending & (1U << Ep)))
    {
      UINT32 Interrupt = Read (DIEPINT (Ep));
      Write (DIEPINT (Ep), Interrupt);
      if (Interrupt & EP_AHB_ERROR)
        mError = EFI_DEVICE_ERROR;
      if (Interrupt & EP_TRANSFER_DONE)
      {
        mTxBusy[Ep] = FALSE;
        if (Ep == 0)
        {
          if (mControlData && mControlOffset < mControlBytes)
            ControlNext ();
          else if (mControlData && mControlZlp)
          {
            mControlZlp = FALSE;
            StartIn (0, mControl, 0);
          }
          else
          {
            mControlData = FALSE;
            ArmControlOut ();
          }
        }
      }
    }
  }
  FillTx (0);
  FillTx (BULK_IN);
}

EFI_STATUS
FastbootUsbStart (VOID)
{
  EFI_STATUS Status = UsbDevicePlatformInit (&mBase);
  if (EFI_ERROR (Status))
    return Status;
  UINT32 Id = Read (GSNPSID);
  if ((Id & 0xffff0000U) != 0x4f540000U)
    return EFI_UNSUPPORTED;
  Write (DCTL, Read (DCTL) | SOFT_DISCONNECT);
  MicroSecondDelay (100000);
  Write (GAHBCFG, 0); // Programmed I/O: DMA stays disabled.
  Status = Wait (GRSTCTL, AHB_IDLE, AHB_IDLE);
  if (EFI_ERROR (Status))
    return Status;
  Write (GRSTCTL, CORE_RESET);
  if ((Id & 0xffff) >= DWC2_REV_420A)
  {
    Status = Wait (GRSTCTL, CORE_RESET_DONE, CORE_RESET_DONE);
    Write (GRSTCTL, (Read (GRSTCTL) & ~CORE_RESET) | CORE_RESET_DONE);
  }
  else
    Status = Wait (GRSTCTL, CORE_RESET, 0);
  if (EFI_ERROR (Status))
    return Status;
  Write (GUSBCFG, FORCE_DEVICE | TX_FIFO_REWIND | TURNAROUND_8BIT | 7);
  MicroSecondDelay (25000);
  Write (DCTL, SOFT_DISCONNECT);
  Write (PCGCCTL, 0);
  UINTN Depth = Read (GHWCFG3) >> 16;
  mDedicated = (Read (GHWCFG4) & DEDICATED_FIFO) != 0;
  if (Depth < 512)
    return EFI_UNSUPPORTED;
  UINTN RxWords = MIN (Depth - (mDedicated ? 256 : 128), 512);
  Write (GRXFSIZ, (UINT32)RxWords);
  Write (GNPTXFSIZ, ((UINT32)(mDedicated ? 128 : Depth - RxWords) << 16)
                        | (UINT32)RxWords);
  if (mDedicated)
    Write (DIEPTXF1, (128U << 16) | (UINT32)(RxWords + 128));
  Write (DCFG, 1U << 18);
  mError = EFI_SUCCESS;
  mBulkPacket = 512;
  mGeneration = mSeenGeneration = 0;
  mStarted = TRUE;
  ResetDevice ();
  Write (GINTSTS, MAX_UINT32);
  Write (GINTMSK, IRQ_RX_FIFO | IRQ_USB_RESET | IRQ_ENUM_DONE | IRQ_IN_EP
                      | IRQ_OUT_EP | IRQ_DISCONNECT);
  Write (DCTL, CLEAR_GLOBAL_IN_NAK | CLEAR_GLOBAL_OUT_NAK);
  return mError;
}

VOID
FastbootUsbStop (VOID)
{
  if (!mStarted)
    return;
  Write (DCTL, Read (DCTL) | SOFT_DISCONNECT);
  Write (GINTMSK, 0);
  Write (GAHBCFG, 0);
  mStarted = mConfigured = FALSE;
  mRxArmed = mRxReady = FALSE;
  mTxBusy[0] = mTxBusy[1] = FALSE;
}

EFI_STATUS
FastbootUsbPoll (VOID)
{
  if (!mStarted)
    return EFI_NOT_STARTED;
  Poll ();
  if (EFI_ERROR (mError))
    return mError;
  if (mGeneration != mSeenGeneration)
  {
    mSeenGeneration = mGeneration;
    return EFI_MEDIA_CHANGED;
  }
  return mConfigured ? EFI_SUCCESS : EFI_NOT_READY;
}

EFI_STATUS
FastbootUsbReceive (VOID *Buffer, UINTN *Length)
{
  EFI_STATUS Status;
  if (Buffer == NULL || Length == NULL || *Length == 0)
    return EFI_INVALID_PARAMETER;
  Status = FastbootUsbPoll ();
  if (EFI_ERROR (Status))
    return Status;
  if (mRxReady)
  {
    if (*Length < mRxBytes)
      return EFI_BUFFER_TOO_SMALL;
    CopyMem (Buffer, mRx, mRxBytes);
    *Length = mRxBytes;
    mRxReady = FALSE;
    return EFI_SUCCESS;
  }
  if (!mRxArmed)
  {
    mRxLimit = MIN (*Length, sizeof (mRx));
    mRxBytes = 0;
    mRxArmed = TRUE;
    UINTN Packets = (mRxLimit + mBulkPacket - 1) / mBulkPacket;
    Write (DOEPINT (BULK_OUT), MAX_UINT32);
    Write (DOEPTSIZ (BULK_OUT),
           (UINT32)(Packets * mBulkPacket)
               | ((UINT32)Packets << XFER_PACKET_SHIFT));
    Write (DOEPCTL (BULK_OUT), (Read (DOEPCTL (BULK_OUT)) & ~EP_SET_NAK)
                                   | EP_ENABLE | EP_CLEAR_NAK);
  }
  return EFI_NOT_READY;
}

EFI_STATUS
FastbootUsbSend (CONST VOID *Buffer, UINTN Length)
{
  if (!mStarted || !mConfigured)
    return EFI_NOT_READY;
  UINT32 Generation = mGeneration;
  for (UINTN Offset = 0; Offset < Length;)
  {
    UINTN Bytes = MIN (Length - Offset, TRANSFER_BYTES);
    StartIn (BULK_IN, (CONST UINT8 *)Buffer + Offset, Bytes);
    UINT64 Start = GetPerformanceCounter ();
    while (mTxBusy[BULK_IN])
    {
      Poll ();
      if (EFI_ERROR (mError))
        return mError;
      if (mGeneration != Generation)
        return EFI_MEDIA_CHANGED;
      if (GetTimeInNanoSecond (GetPerformanceCounter () - Start)
          > 5000000000ULL)
        return EFI_TIMEOUT;
    }
    Offset += Bytes;
  }
  return EFI_SUCCESS;
}
