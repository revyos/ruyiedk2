// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleNetwork.h>

#include <Protocol/FdtClient.h>
typedef struct
{
  UINTN Base, Apb, MdioBase, Index, PhyAddress;
  UINT64 Clock;
  BOOLEAN TxDelay, RxDelay;
  VOID *Fdt;
} GMAC_HARDWARE;
#define MAC_CONFIG 0x0000
#define MAC_FILTER 0x0004
#define MAC_HASH_HIGH 0x0008
#define MAC_HASH_LOW 0x000c
#define MAC_MDIO_ADDRESS 0x0010
#define MAC_MDIO_DATA 0x0014
#define MAC_FLOW_CONTROL 0x0018
#define MAC_VERSION 0x0020
#define MAC_INTERRUPT_MASK 0x003c
#define MAC_ADDRESS_HIGH 0x0040
#define MAC_ADDRESS_LOW 0x0044
#define DMA_BUS_MODE 0x1000
#define DMA_TX_POLL 0x1004
#define DMA_RX_POLL 0x1008
#define DMA_RX_BASE 0x100c
#define DMA_TX_BASE 0x1010
#define DMA_STATUS 0x1014
#define DMA_OPERATION 0x1018
#define DMA_INTERRUPT_ENABLE 0x101c
#define DESC_OWN BIT31
#define RX_FIRST BIT9
#define RX_LAST BIT8
#define DESC_ERROR BIT15
#define RX_CHAIN BIT14
#define TX_CHAIN BIT20
#define TX_FIRST BIT28
#define TX_LAST BIT29
#define RX_COUNT 16
#define BUFFER_SIZE 2048
#define MAX_FRAME 1518

STATIC inline UINT32
GmacRead (GMAC_HARDWARE *Hw, UINTN Offset)
{
  return MmioRead32 (Hw->Base + Offset);
}
STATIC inline VOID
GmacWrite (GMAC_HARDWARE *Hw, UINTN Offset, UINT32 Value)
{
  MmioWrite32 (Hw->Base + Offset, Value);
}

EFI_STATUS
GmacPlatformInitialize (GMAC_HARDWARE *Hw);
EFI_STATUS
GmacPhyInitialize (GMAC_HARDWARE *Hw);
EFI_STATUS
GmacLink (GMAC_HARDWARE *Hw, BOOLEAN *Up, UINTN *Speed, BOOLEAN *Duplex);
EFI_STATUS
GmacSetSpeed (GMAC_HARDWARE *Hw, UINTN Speed);

EFI_STATUS
GmacReadFactoryAddress (GMAC_HARDWARE *Hw, UINT8 *Mac);

EFI_STATUS
GmacParseHardware (FDT_CLIENT_PROTOCOL *Client, INT32 Node, GMAC_HARDWARE *Hw);
EFI_STATUS
GmacFindNode (GMAC_HARDWARE *Hw, INT32 *Node);
EFI_STATUS
GmacConfigurePins (GMAC_HARDWARE *Hw);

EFI_STATUS GmacFallbackAddress (UINTN Index, UINT8 *Mac);
