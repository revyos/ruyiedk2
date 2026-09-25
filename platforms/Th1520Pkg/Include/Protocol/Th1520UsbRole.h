// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
typedef struct _TH1520_USB_ROLE_PROTOCOL TH1520_USB_ROLE_PROTOCOL;
// Platform-private ownership transfer for the shared DWC3 controller.
// Call at TPL_APPLICATION while boot services are available.
struct _TH1520_USB_ROLE_PROTOCOL
{
  // Disconnect host drivers and select USB-C. On success the caller owns
  // the controller and may initialize the device transport.
  EFI_STATUS (EFIAPI *EnterDevice) (TH1520_USB_ROLE_PROTOCOL *This);
  // Call only after stopping gadget DMA. Reconnect USB-A host drivers;
  // downstream enumeration may complete asynchronously.
  EFI_STATUS (EFIAPI *LeaveDevice) (TH1520_USB_ROLE_PROTOCOL *This);
};
extern EFI_GUID gTh1520UsbRoleProtocolGuid;
