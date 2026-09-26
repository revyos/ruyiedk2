// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once

#define GAHBCFG 0x008
#define GUSBCFG 0x00c
#define GRSTCTL 0x010
#define GINTSTS 0x014
#define GINTMSK 0x018
#define GRXSTSP 0x020
#define GRXFSIZ 0x024
#define GNPTXFSIZ 0x028
#define GNPTXSTS 0x02c
#define GSNPSID 0x040
#define GHWCFG3 0x04c
#define GHWCFG4 0x050
#define DIEPTXF1 0x104
#define DCFG 0x800
#define DCTL 0x804
#define DSTS 0x808
#define DIEPMSK 0x810
#define DOEPMSK 0x814
#define DAINT 0x818
#define DAINTMSK 0x81c
#define DIEPCTL(N) (0x900 + (N) * 0x20)
#define DIEPINT(N) (0x908 + (N) * 0x20)
#define DIEPTSIZ(N) (0x910 + (N) * 0x20)
#define DTXFSTS(N) (0x918 + (N) * 0x20)
#define DOEPCTL(N) (0xb00 + (N) * 0x20)
#define DOEPINT(N) (0xb08 + (N) * 0x20)
#define DOEPTSIZ(N) (0xb10 + (N) * 0x20)
#define PCGCCTL 0xe00
#define FIFO(N) (0x1000 + (N) * 0x1000)

#define CORE_RESET BIT0
#define RX_FIFO_FLUSH BIT4
#define TX_FIFO_FLUSH BIT5
#define ALL_TX_FIFOS (0x10U << 6)
#define CORE_RESET_DONE BIT29
#define AHB_IDLE BIT31
#define FORCE_DEVICE BIT30
#define TX_FIFO_REWIND BIT14
#define TURNAROUND_8BIT (9U << 10)
#define SOFT_DISCONNECT BIT1
#define CLEAR_GLOBAL_IN_NAK BIT8
#define CLEAR_GLOBAL_OUT_NAK BIT10
#define DEDICATED_FIFO BIT25
#define IRQ_RX_FIFO BIT4
#define IRQ_USB_RESET BIT12
#define IRQ_ENUM_DONE BIT13
#define IRQ_IN_EP BIT18
#define IRQ_OUT_EP BIT19
#define IRQ_DISCONNECT BIT29
#define EP_TRANSFER_DONE BIT0
#define EP_AHB_ERROR BIT2
#define EP_SETUP_DONE BIT3
#define EP_ACTIVE BIT15
#define EP_STALL BIT21
#define EP_CLEAR_NAK BIT26
#define EP_SET_NAK BIT27
#define EP_DATA0 BIT28
#define EP_ENABLE BIT31
#define EP_TYPE_BULK (2U << 18)
#define EP_FIFO_SHIFT 22
#define EP_NEXT_SHIFT 11
#define XFER_PACKET_SHIFT 19
#define SETUP_COUNT (3U << 29)
#define DCFG_ADDRESS_MASK (0x7fU << 4)
#define DSTS_SPEED_SHIFT 1
#define DSTS_SPEED_MASK 3
#define RX_PACKET_DATA 2
#define RX_PACKET_SETUP 6
#define RX_COUNT_SHIFT 4
#define RX_COUNT_MASK 0x7ff
#define RX_STATUS_SHIFT 17
#define RX_STATUS_MASK 15
#define DWC2_REV_420A 0x420a
#define EP0_PACKET 64
#define BULK_IN 1
#define BULK_OUT 2
#define TRANSFER_BYTES SIZE_16KB
