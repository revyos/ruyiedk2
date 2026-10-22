// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef A210_SCMI_PROTOCOL_H
#define A210_SCMI_PROTOCOL_H

#include <Uefi.h>

#define A210_SCMI_PAYLOAD_SIZE 228

typedef struct _A210_SCMI_PROTOCOL A210_SCMI_PROTOCOL;

// Buffer contains request words on entry and the complete response payload
// (including the SCMI status word) on return. Transport errors are EFI_STATUS;
// remote protocol errors remain in Buffer[0]. Calls are boot-services only.
typedef EFI_STATUS (EFIAPI *A210_SCMI_EXECUTE) (
  IN A210_SCMI_PROTOCOL *This,
  IN UINT8 Protocol,
  IN UINT8 Message,
  IN OUT UINT32 *Buffer,
  IN UINT32 RequestSize,
  IN OUT UINT32 *ResponseSize
  );

struct _A210_SCMI_PROTOCOL {
  A210_SCMI_EXECUTE Execute;
};

extern EFI_GUID gA210ScmiProtocolGuid;

#endif
