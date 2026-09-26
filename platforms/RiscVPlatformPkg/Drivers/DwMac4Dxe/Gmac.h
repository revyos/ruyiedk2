// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>
#include <Library/EqosPlatformLib.h>
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
#define MAC_CONFIG 0x0000
#define MAC_FILTER 0x0008
#define MAC_HASH_HIGH 0x0014
#define MAC_HASH_LOW 0x0010
#define MAC_MDIO_ADDRESS 0x0200
#define MAC_MDIO_DATA 0x0204
#define MAC_FLOW_CONTROL 0x0070
#define MAC_RX_FLOW_CONTROL 0x0090
#define MAC_RX_QUEUE 0x00a0
#define MAC_VERSION 0x0110
#define MAC_FEATURE1 0x0120
#define MAC_INTERRUPT_MASK 0x00b4
#define MAC_ADDRESS_HIGH 0x0300
#define MAC_ADDRESS_LOW 0x0304
#define MTL_TX_OPERATION 0x0d00
#define MTL_TX_WEIGHT 0x0d18
#define MTL_RX_OPERATION 0x0d30
#define DMA_BUS_MODE 0x1000
#define DMA_SYSBUS_MODE 0x1004
#define DMA_CHANNEL 0x1100
#define DMA_TX_CONTROL 0x1104
#define DMA_RX_CONTROL 0x1108
#define DMA_TX_HIGH 0x1110
#define DMA_TX_BASE 0x1114
#define DMA_RX_HIGH 0x1118
#define DMA_RX_BASE 0x111c
#define DMA_TX_TAIL 0x1120
#define DMA_RX_TAIL 0x1128
#define DMA_TX_LENGTH 0x112c
#define DMA_RX_LENGTH 0x1130
#define DMA_INTERRUPT_ENABLE 0x1134
#define DMA_STATUS 0x1160
#define DESC_OWN BIT31
#define RX_FIRST BIT29
#define RX_LAST BIT28
#define DESC_ERROR BIT15
#define RX_BUFFER_VALID BIT24
#define TX_FIRST BIT29
#define TX_LAST BIT28
#define RX_COUNT 64
#define TX_COUNT 8
#define BUFFER_SIZE 2048
#define MAX_FRAME 1518
#define DMA_AXI_BYTES 8
#define DMA_DESC_SKIP_SHIFT 18

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
STATIC inline BOOLEAN
GmacValidAddress (CONST UINT8 *Mac)
{
  CONST UINT8 Zero[6] = { 0 };
  return !(Mac[0] & 1) && CompareMem (Mac, Zero, 6) != 0;
}
EFI_STATUS GmacPhyInitialize (GMAC_HARDWARE *Hw);
EFI_STATUS
GmacLink (GMAC_HARDWARE *Hw, BOOLEAN *Up, UINTN *Speed, BOOLEAN *Duplex);
