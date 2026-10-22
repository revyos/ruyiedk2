// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/ArmMtlLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/A210Scmi.h>

// MTL exposes a local staging buffer. Only the transport protocol accesses
// shared SRAM, serializing and completing the entire transaction at TPL_NOTIFY.
STATIC UINT32 mStorage[64];
STATIC MTL_CHANNEL mChannel = {
  MTL_CHANNEL_TYPE_LOW, (MTL_MAILBOX *)mStorage, { 0, 0, 0 }
};
STATIC A210_SCMI_PROTOCOL *mTransport;
STATIC UINT32 mResponseHeader, mResponseSize;
STATIC BOOLEAN mResponseReady;

EFI_STATUS
MtlGetChannel (MTL_CHANNEL_TYPE Type, MTL_CHANNEL **Channel)
{
  EFI_STATUS Status;
  if (Type != MTL_CHANNEL_TYPE_LOW || !Channel)
    return EFI_UNSUPPORTED;
  if (!mTransport)
  {
    Status = gBS->LocateProtocol (&gA210ScmiProtocolGuid, NULL,
                                  (VOID **)&mTransport);
    if (EFI_ERROR (Status))
      return Status;
  }
  *Channel = &mChannel;
  return EFI_SUCCESS;
}

UINT32 *
MtlGetChannelPayload (MTL_CHANNEL *Channel)
{
  return Channel == &mChannel ? &mStorage[7] : NULL;
}

EFI_STATUS
MtlWaitUntilChannelFree (MTL_CHANNEL *Channel, UINTN Timeout)
{
  return Channel == &mChannel ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

EFI_STATUS
MtlSendMessage (MTL_CHANNEL *Channel, UINT32 Header, UINT32 Length)
{
  EFI_STATUS Status;
  if (Channel != &mChannel || !mTransport || (Header & ~0x3fcffU))
    return EFI_INVALID_PARAMETER;
  mResponseReady = FALSE;
  mResponseSize = A210_SCMI_PAYLOAD_SIZE;
  Status = mTransport->Execute (mTransport, (Header >> 10) & 0xff,
                                Header & 0xff, MtlGetChannelPayload (Channel),
                                Length, &mResponseSize);
  if (!EFI_ERROR (Status))
  {
    mResponseHeader = Header;
    mResponseReady = TRUE;
  }
  return Status;
}

EFI_STATUS
MtlReceiveMessage (MTL_CHANNEL *Channel, UINT32 *Header, UINT32 *Length)
{
  if (Channel != &mChannel || !Header || !Length)
    return EFI_INVALID_PARAMETER;
  if (!mResponseReady)
    return EFI_NOT_READY;
  *Header = mResponseHeader;
  *Length = mResponseSize;
  mResponseReady = FALSE;
  return EFI_SUCCESS;
}
