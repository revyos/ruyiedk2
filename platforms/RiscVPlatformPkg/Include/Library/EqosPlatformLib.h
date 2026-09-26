// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef EQOS_PLATFORM_LIB_H_
#define EQOS_PLATFORM_LIB_H_
#include <Uefi.h>
#include <Protocol/FdtClient.h>

typedef struct
{
  UINTN Base, Apb, MdioBase, Index, PhyAddress;
  UINT64 Clock;
  BOOLEAN TxDelay, RxDelay;
  VOID *Fdt;
  INT32 Node;
} GMAC_HARDWARE;

EFI_STATUS
GmacParseHardware (FDT_CLIENT_PROTOCOL *Fdt, INT32 Node,
                   GMAC_HARDWARE *Hw);
EFI_STATUS GmacPlatformInitialize (GMAC_HARDWARE *Hw);
EFI_STATUS GmacSetSpeed (GMAC_HARDWARE *Hw, UINTN Speed);
EFI_STATUS
GmacSelectAddress (FDT_CLIENT_PROTOCOL *Fdt, GMAC_HARDWARE *Hw,
                   UINT8 *Address);
#endif
