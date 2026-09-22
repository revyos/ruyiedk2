// SPDX-License-Identifier: BSD-2-Clause-Patent
// Per-controller, polled DWMAC 3.70a SNP instances discovered from the DTB.
#include "Gmac.h"
#include <Library/Th1520MacStoreLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FdtClient.h>

#define FILTER_MASK                                                                                \
  (EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |                     \
   EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST | EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS |                 \
   EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)

typedef struct
{
  volatile UINT32 Status;
  UINT32 Control, Buffer, Next;
  UINT8 Padding[48];
} GMAC_DESCRIPTOR;
STATIC_ASSERT (sizeof (GMAC_DESCRIPTOR) == 64, "One descriptor per C910 cache line");
typedef struct
{
  GMAC_DESCRIPTOR Rx[RX_COUNT];
  GMAC_DESCRIPTOR Tx[TX_COUNT];
  UINT8 RxBuffer[RX_COUNT][BUFFER_SIZE];
  UINT8 TxBuffer[TX_COUNT][BUFFER_SIZE];
} GMAC_DMA;
#pragma pack(1)
typedef struct
{
  MAC_ADDR_DEVICE_PATH Mac;
  EFI_DEVICE_PATH_PROTOCOL End;
} GMAC_DEVICE_PATH;
#pragma pack()
typedef struct
{
  LIST_ENTRY Link;
  GMAC_HARDWARE Hardware;
  EFI_SIMPLE_NETWORK_PROTOCOL Snp;
  EFI_SIMPLE_NETWORK_MODE Mode;
  EFI_LOCK Lock;
  GMAC_DEVICE_PATH Path;
  EFI_HANDLE Handle;
  EFI_EVENT PollEvent, ExitEvent;
  GMAC_DMA *Dma;
  UINTN RxIndex, Speed;
  VOID *TxCookie[TX_COUNT];
  UINTN TxHead, TxTail, TxPending;
  BOOLEAN TxComplete, Duplex, Exited;
  UINT64 NextLinkCheck, CounterFrequency;
} GMAC_DEVICE;
STATIC LIST_ENTRY Devices = INITIALIZE_LIST_HEAD_VARIABLE (Devices);

STATIC GMAC_DEVICE *
FindNic (EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
  LIST_ENTRY *Entry;
  for (Entry = GetFirstNode (&Devices); !IsNull (&Devices, Entry);
       Entry = GetNextNode (&Devices, Entry))
  {
    GMAC_DEVICE *Nic = BASE_CR (Entry, GMAC_DEVICE, Link);
    if (This == &Nic->Snp)
    {
      return Nic;
    }
  }
  return NULL;
}

STATIC EFI_STATUS
LockInitialized (EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EfiAcquireLock (&Nic->Lock);
  if (Nic->Mode.State == EfiSimpleNetworkInitialized && !Nic->Exited)
  {
    return EFI_SUCCESS;
  }
  EfiReleaseLock (&Nic->Lock);
  return Nic->Mode.State == EfiSimpleNetworkStopped ? EFI_NOT_STARTED : EFI_DEVICE_ERROR;
}
STATIC BOOLEAN
Broadcast (CONST UINT8 *Mac)
{
  STATIC CONST UINT8 All[6] = {255, 255, 255, 255, 255, 255};
  return CompareMem (Mac, All, 6) == 0;
}
STATIC VOID
SetAddress (GMAC_DEVICE *Nic)
{
  CONST UINT8 *Mac = Nic->Mode.CurrentAddress.Addr;
  GmacWrite (&Nic->Hardware, MAC_ADDRESS_HIGH, ((UINT32)Mac[5] << 8) | Mac[4]);
  GmacWrite (&Nic->Hardware, MAC_ADDRESS_LOW,
             ((UINT32)Mac[3] << 24) | ((UINT32)Mac[2] << 16) | ((UINT32)Mac[1] << 8) | Mac[0]);
}
STATIC VOID
SetFilters (GMAC_DEVICE *Nic)
{
  UINT32 Filter = BIT10, Hash[2] = {0, 0}, Crc, Bit;
  UINTN Index, I, J;
  if (!(Nic->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST))
  {
    Filter |= BIT5;
  }
  if (Nic->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS)
  {
    Filter |= BIT0;
  }
  if (Nic->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)
  {
    Filter |= BIT4;
  }
  if (Nic->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST)
  {
    Filter |= BIT2;
    for (Index = 0; Index < Nic->Mode.MCastFilterCount; Index++)
    {
      Crc = MAX_UINT32;
      for (I = 0; I < 6; I++)
      {
        Crc ^= Nic->Mode.MCastFilter[Index].Addr[I];
        for (J = 0; J < 8; J++)
        {
          Crc = (Crc >> 1) ^ ((Crc & 1) ? 0xedb88320U : 0);
        }
      }
      Crc = ~Crc;
      Bit = 0;
      for (I = 0; I < 6; I++)
      {
        Bit = (Bit << 1) | ((Crc >> I) & 1);
      }
      Hash[Bit >> 5] |= 1U << (Bit & 31);
    }
  }
  GmacWrite (&Nic->Hardware, MAC_HASH_LOW, Hash[0]);
  GmacWrite (&Nic->Hardware, MAC_HASH_HIGH, Hash[1]);
  GmacWrite (&Nic->Hardware, MAC_FILTER, Filter);
}
STATIC VOID
UpdateLink (GMAC_DEVICE *Nic, BOOLEAN Force)
{
  UINT64 Now = GetPerformanceCounter ();
  UINTN Speed = 0;
  BOOLEAN Up, Duplex = FALSE;
  UINT32 Config;
  if (!Force && Now < Nic->NextLinkCheck)
  {
    return;
  }
  Nic->NextLinkCheck = Now + Nic->CounterFrequency / 4;
  if (EFI_ERROR (GmacLink (&Nic->Hardware, &Up, &Speed, &Duplex)))
  {
    Up = FALSE;
  }
  if (Up && (Force || !Nic->Mode.MediaPresent || Speed != Nic->Speed || Duplex != Nic->Duplex))
  {
    if (EFI_ERROR (GmacSetSpeed (&Nic->Hardware, Speed)))
    {
      Up = FALSE;
    }
    else
    {
      Config = GmacRead (&Nic->Hardware, MAC_CONFIG) & ~(BIT15 | BIT14 | BIT11);
      if (Speed != 1000)
      {
        Config |= BIT15;
      }
      if (Speed == 100)
      {
        Config |= BIT14;
      }
      if (Duplex)
      {
        Config |= BIT11;
      }
      GmacWrite (&Nic->Hardware, MAC_CONFIG, Config);
      Nic->Speed = Speed;
      Nic->Duplex = Duplex;
      DEBUG ((DEBUG_INFO, "TH1520 GMAC: %u Mbps, %a duplex\n", Speed, Duplex ? "full" : "half"));
    }
  }
  Nic->Mode.MediaPresent = Up;
}
STATIC EFI_STATUS
HaltDma (GMAC_DEVICE *Nic)
{
  UINTN Retry;
  UINT32 State;
  GmacWrite (&Nic->Hardware, DMA_INTERRUPT_ENABLE, 0);
  GmacWrite (&Nic->Hardware, MAC_CONFIG, GmacRead (&Nic->Hardware, MAC_CONFIG) & ~(BIT2 | BIT3));
  GmacWrite (&Nic->Hardware, DMA_OPERATION,
             GmacRead (&Nic->Hardware, DMA_OPERATION) & ~(BIT1 | BIT13));
  for (Retry = 0; Retry < 10000; Retry++)
  {
    State = GmacRead (&Nic->Hardware, DMA_STATUS);
    if (!(State & ((7U << 17) | (7U << 20))))
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  // Reset aborts outstanding transfers if a channel does not stop normally.
  GmacWrite (&Nic->Hardware, DMA_BUS_MODE, GmacRead (&Nic->Hardware, DMA_BUS_MODE) | BIT0);
  for (Retry = 0; Retry < 10000; Retry++)
  {
    if (!(GmacRead (&Nic->Hardware, DMA_BUS_MODE) & BIT0))
    {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}
STATIC VOID
RearmRx (GMAC_DEVICE *Nic, UINTN Index)
{
  GMAC_DESCRIPTOR *Desc = &Nic->Dma->Rx[Index];
  WriteBackInvalidateDataCacheRange (Nic->Dma->RxBuffer[Index], BUFFER_SIZE);
  Desc->Control = RX_CHAIN | BUFFER_SIZE;
  Desc->Buffer = (UINT32)(UINTN)Nic->Dma->RxBuffer[Index];
  Desc->Next = (UINT32)(UINTN)&Nic->Dma->Rx[(Index + 1) % RX_COUNT];
  Desc->Status = DESC_OWN;
  WriteBackInvalidateDataCacheRange (Desc, sizeof (*Desc));
}
STATIC EFI_STATUS
InitializeHardware (GMAC_DEVICE *Nic)
{
  UINTN Index;
  EFI_STATUS Status;
  Status = GmacSetSpeed (&Nic->Hardware, 1000);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  GmacWrite (&Nic->Hardware, MAC_CONFIG, 0);
  GmacWrite (&Nic->Hardware, DMA_BUS_MODE, BIT0);
  for (Index = 0; Index < 10000; Index++)
  {
    if (!(GmacRead (&Nic->Hardware, DMA_BUS_MODE) & BIT0))
    {
      break;
    }
    MicroSecondDelay (10);
  }
  if (Index == 10000)
  {
    return EFI_TIMEOUT;
  }
  ZeroMem (Nic->Dma, sizeof (*Nic->Dma));
  Nic->RxIndex = 0;
  for (Index = 0; Index < RX_COUNT; Index++)
  {
    RearmRx (Nic, Index);
  }
  for (Index = 0; Index < TX_COUNT; Index++)
  {
    Nic->Dma->Tx[Index].Status = TX_CHAIN;
    Nic->Dma->Tx[Index].Buffer = (UINT32)(UINTN)Nic->Dma->TxBuffer[Index];
    Nic->Dma->Tx[Index].Next =
        (UINT32)(UINTN)&Nic->Dma->Tx[(Index + 1) % TX_COUNT];
  }
  WriteBackInvalidateDataCacheRange (Nic->Dma->Tx, sizeof (Nic->Dma->Tx));
  GmacWrite (&Nic->Hardware, DMA_BUS_MODE, BIT16 | (8 << 8));
  GmacWrite (&Nic->Hardware, DMA_RX_BASE, (UINT32)(UINTN)&Nic->Dma->Rx[0]);
  // Reset retains caller cookies; hardware starts at the next free slot.
  GmacWrite (&Nic->Hardware, DMA_TX_BASE,
             (UINT32)(UINTN)&Nic->Dma->Tx[Nic->TxHead]);
  GmacWrite (&Nic->Hardware, DMA_INTERRUPT_ENABLE, 0);
  GmacWrite (&Nic->Hardware, MAC_INTERRUPT_MASK, MAX_UINT32);
  GmacWrite (&Nic->Hardware, DMA_STATUS, MAX_UINT32);
  GmacWrite (&Nic->Hardware, MAC_FLOW_CONTROL, 0);
  SetAddress (Nic);
  SetFilters (Nic);
  GmacWrite (&Nic->Hardware, MAC_CONFIG, BIT21 | BIT13 | BIT11 | BIT3 | BIT2);
  UpdateLink (Nic, TRUE);
  GmacWrite (&Nic->Hardware, DMA_OPERATION, BIT25 | BIT21 | BIT13 | BIT1);
  GmacWrite (&Nic->Hardware, DMA_RX_POLL, 1);
  return EFI_SUCCESS;
}
STATIC BOOLEAN
RxReady (GMAC_DEVICE *Nic)
{
  GMAC_DESCRIPTOR *Desc = &Nic->Dma->Rx[Nic->RxIndex];
  InvalidateDataCacheRange (Desc, sizeof (*Desc));
  return !(Desc->Status & DESC_OWN);
}
STATIC VOID
CheckTx (GMAC_DEVICE *Nic)
{
  if (!Nic->TxPending || Nic->TxComplete)
  {
    return;
  }
  InvalidateDataCacheRange (&Nic->Dma->Tx[Nic->TxTail],
                            sizeof (Nic->Dma->Tx[0]));
  if (Nic->Dma->Tx[Nic->TxTail].Status & DESC_OWN)
  {
    return;
  }
  Nic->TxComplete = TRUE;
}
STATIC VOID EFIAPI
Poll (EFI_EVENT Event, VOID *Context)
{
  GMAC_DEVICE *Nic = Context;
  if (Nic->Exited || Nic->Mode.State != EfiSimpleNetworkInitialized)
  {
    return;
  }
  EfiAcquireLock (&Nic->Lock);
  UpdateLink (Nic, FALSE);
  if (RxReady (Nic))
  {
    gBS->SignalEvent (Nic->Snp.WaitForPacket);
  }
  EfiReleaseLock (&Nic->Lock);
}
STATIC VOID EFIAPI
ExitBootServices (EFI_EVENT Event, VOID *Context)
{
  GMAC_DEVICE *Nic = Context;
  Nic->Exited = TRUE;
  HaltDma (Nic);
}
STATIC EFI_STATUS EFIAPI
Start (EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status;
  EfiAcquireLock (&Nic->Lock);
  if (Nic->Exited)
  {
    Status = EFI_DEVICE_ERROR;
  }
  else if (Nic->Mode.State != EfiSimpleNetworkStopped)
  {
    Status = EFI_ALREADY_STARTED;
  }
  else
  {
    Nic->Mode.State = EfiSimpleNetworkStarted;
    Status = EFI_SUCCESS;
  }
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Stop (EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status;
  EfiAcquireLock (&Nic->Lock);
  if (Nic->Mode.State == EfiSimpleNetworkStopped)
  {
    Status = EFI_NOT_STARTED;
  }
  else if (Nic->Mode.State == EfiSimpleNetworkInitialized)
  {
    Status = EFI_DEVICE_ERROR;
  }
  else
  {
    Nic->Mode.State = EfiSimpleNetworkStopped;
    Status = EFI_SUCCESS;
  }
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Initialize (EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN ExtraRx, UINTN ExtraTx)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status;
  EfiAcquireLock (&Nic->Lock);
  if (Nic->Exited)
  {
    Status = EFI_DEVICE_ERROR;
  }
  else if (Nic->Mode.State == EfiSimpleNetworkStopped)
  {
    Status = EFI_NOT_STARTED;
  }
  else if (Nic->Mode.State == EfiSimpleNetworkInitialized)
  {
    Status = EFI_SUCCESS;
  }
  else
  {
    ZeroMem (Nic->TxCookie, sizeof (Nic->TxCookie));
    Nic->TxHead = Nic->TxTail = Nic->TxPending = 0;
    Nic->TxComplete = FALSE;
    Status = InitializeHardware (Nic);
    if (!EFI_ERROR (Status))
    {
      Nic->Mode.State = EfiSimpleNetworkInitialized;
      Status = gBS->SetTimer (Nic->PollEvent, TimerPeriodic, 100000); // 10 ms
      if (EFI_ERROR (Status))
      {
        HaltDma (Nic);
        Nic->Mode.State = EfiSimpleNetworkStarted;
      }
    }
  }
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Shutdown (EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = HaltDma (Nic);
  if (!EFI_ERROR (Status))
  {
    gBS->SetTimer (Nic->PollEvent, TimerCancel, 0);
    Nic->Mode.State = EfiSimpleNetworkStarted;
    ZeroMem (Nic->TxCookie, sizeof (Nic->TxCookie));
    Nic->TxHead = Nic->TxTail = Nic->TxPending = 0;
    Nic->TxComplete = FALSE;
    Nic->Mode.ReceiveFilterSetting = 0;
    Nic->Mode.MCastFilterCount = 0;
  }
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Reset (EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Extended)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = HaltDma (Nic);
  if (!EFI_ERROR (Status))
  {
    if (Nic->TxPending && !Nic->TxComplete)
    {
      Nic->TxComplete = TRUE;
    }
    Status = InitializeHardware (Nic);
  }
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
ReceiveFilters (EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 Enable, UINT32 Disable, BOOLEAN ResetList,
                UINTN Count, EFI_MAC_ADDRESS *List)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  UINTN Index;
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if ((Enable | Disable) & ~FILTER_MASK)
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  if (!ResetList && (Enable & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST))
  {
    if (!Count || Count > Nic->Mode.MaxMCastFilterCount || !List)
    {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }
    for (Index = 0; Index < Count; Index++)
    {
      if (!(List[Index].Addr[0] & 1))
      {
        Status = EFI_INVALID_PARAMETER;
        goto Done;
      }
    }
    CopyMem (Nic->Mode.MCastFilter, List, Count * sizeof (*List));
    Nic->Mode.MCastFilterCount = (UINT32)Count;
  }
  if (ResetList)
  {
    ZeroMem (Nic->Mode.MCastFilter, sizeof (Nic->Mode.MCastFilter));
    Nic->Mode.MCastFilterCount = 0;
  }
  Nic->Mode.ReceiveFilterSetting = (Nic->Mode.ReceiveFilterSetting | Enable) & ~Disable;
  SetFilters (Nic);
Done:
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
StationAddress (EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Restore, EFI_MAC_ADDRESS *Address)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (Restore)
  {
    CopyMem (&Nic->Mode.CurrentAddress, &Nic->Mode.PermanentAddress, sizeof (EFI_MAC_ADDRESS));
  }
  else if (!Address || !Th1520MacValid (Address->Addr))
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  else
  {
    CopyMem (&Nic->Mode.CurrentAddress, Address, sizeof (*Address));
  }
  SetAddress (Nic);
Done:
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Statistics (EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Clear, UINTN *Size,
            EFI_NETWORK_STATISTICS *Stats)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  EfiReleaseLock (&Nic->Lock);
  return EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI
MCastIpToMac (EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Ipv6, EFI_IP_ADDRESS *Ip,
              EFI_MAC_ADDRESS *Mac)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (!Ip || !Mac)
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  ZeroMem (Mac, sizeof (*Mac));
  if (Ipv6)
  {
    if (Ip->v6.Addr[0] != 0xff)
    {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }
    Mac->Addr[0] = Mac->Addr[1] = 0x33;
    CopyMem (&Mac->Addr[2], &Ip->v6.Addr[12], 4);
  }
  else
  {
    if ((Ip->v4.Addr[0] & 0xf0) != 0xe0)
    {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }
    Mac->Addr[0] = 1;
    Mac->Addr[2] = 0x5e;
    Mac->Addr[3] = Ip->v4.Addr[1] & 0x7f;
    Mac->Addr[4] = Ip->v4.Addr[2];
    Mac->Addr[5] = Ip->v4.Addr[3];
  }
Done:
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
NvData (EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Read, UINTN Offset, UINTN Size, VOID *Buffer)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  EfiReleaseLock (&Nic->Lock);
  return EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI
GetStatus (EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 *Interrupt, VOID **TxBuffer)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  UpdateLink (Nic, FALSE);
  CheckTx (Nic);
  if (Interrupt)
  {
    UINT32 Pending = GmacRead (&Nic->Hardware, DMA_STATUS);
    *Interrupt = (Pending & BIT6) ? EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT : 0;
    if (Pending & BIT0)
    {
      *Interrupt |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
    }
    // Reading the recycled TX buffer alone must not clear interrupt status.
    GmacWrite (&Nic->Hardware, DMA_STATUS, Pending & 0x1ffff);
  }
  if (TxBuffer)
  {
    *TxBuffer = Nic->TxComplete ? Nic->TxCookie[Nic->TxTail] : NULL;
    if (*TxBuffer)
    {
      Nic->TxCookie[Nic->TxTail] = NULL;
      Nic->TxTail = (Nic->TxTail + 1) % TX_COUNT;
      Nic->TxPending--;
      Nic->TxComplete = FALSE;
    }
  }
  EfiReleaseLock (&Nic->Lock);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
Transmit (EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN HeaderSize, UINTN Size, VOID *Buffer,
          EFI_MAC_ADDRESS *Source, EFI_MAC_ADDRESS *Destination, UINT16 *Protocol)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  UINT8 *Bytes = Buffer;
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (!Buffer || (HeaderSize && HeaderSize != 14) || (HeaderSize && (!Destination || !Protocol)))
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  if (Size < 14)
  {
    Status = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }
  if (Size > MAX_FRAME)
  {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }
  UpdateLink (Nic, FALSE);
  if (!Nic->Mode.MediaPresent)
  {
    Status = EFI_NOT_READY;
    goto Done;
  }
  if (Nic->TxPending == TX_COUNT)
  {
    Status = EFI_NOT_READY;
    goto Done;
  }
  if (HeaderSize)
  {
    CopyMem (Bytes, Destination->Addr, 6);
    CopyMem (Bytes + 6, Source ? Source->Addr : Nic->Mode.CurrentAddress.Addr, 6);
    Bytes[12] = *Protocol >> 8;
    Bytes[13] = *Protocol;
  }
  UINTN Slot = Nic->TxHead;
  CopyMem (Nic->Dma->TxBuffer[Slot], Buffer, Size);
  WriteBackInvalidateDataCacheRange (Nic->Dma->TxBuffer[Slot],
                                     ALIGN_VALUE (Size, 64));
  Nic->Dma->Tx[Slot].Control = (UINT32)Size;
  Nic->Dma->Tx[Slot].Status =
      DESC_OWN | BIT30 | TX_FIRST | TX_LAST | TX_CHAIN;
  WriteBackInvalidateDataCacheRange (&Nic->Dma->Tx[Slot],
                                     sizeof (Nic->Dma->Tx[Slot]));
  Nic->TxCookie[Slot] = Buffer;
  Nic->TxHead = (Slot + 1) % TX_COUNT;
  Nic->TxPending++;
  GmacWrite (&Nic->Hardware, DMA_TX_POLL, 1);
Done:
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC BOOLEAN
Accept (GMAC_DEVICE *Nic, CONST UINT8 *Frame)
{
  UINT32 Filters = Nic->Mode.ReceiveFilterSetting;
  UINTN Index;
  if (Filters & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS)
  {
    return TRUE;
  }
  if (Broadcast (Frame))
  {
    return (Filters & EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST) != 0;
  }
  if (Frame[0] & 1)
  {
    if (Filters & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)
    {
      return TRUE;
    }
    if (!(Filters & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST))
    {
      return FALSE;
    }
    for (Index = 0; Index < Nic->Mode.MCastFilterCount; Index++)
    {
      if (!CompareMem (Frame, Nic->Mode.MCastFilter[Index].Addr, 6))
      {
        return TRUE;
      }
    }
    return FALSE;
  }
  return (Filters & EFI_SIMPLE_NETWORK_RECEIVE_UNICAST) &&
         !CompareMem (Frame, Nic->Mode.CurrentAddress.Addr, 6);
}
STATIC EFI_STATUS EFIAPI
Receive (EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN *HeaderSize, UINTN *Size, VOID *Buffer,
         EFI_MAC_ADDRESS *Source, EFI_MAC_ADDRESS *Destination, UINT16 *Protocol)
{
  GMAC_DEVICE *Nic = FindNic (This);
  if (Nic == NULL)
  {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = LockInitialized (This);
  UINTN Count, Length;
  UINT32 Flags;
  UINT8 *Frame;
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (!Size || !Buffer)
  {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  for (Count = 0; Count < RX_COUNT && RxReady (Nic); Count++)
  {
    Flags = Nic->Dma->Rx[Nic->RxIndex].Status;
    Length = (Flags >> 16) & 0x3fff;
    Frame = Nic->Dma->RxBuffer[Nic->RxIndex];
    InvalidateDataCacheRange (Frame, BUFFER_SIZE);
    if (!(Flags & DESC_ERROR) && (Flags & (RX_FIRST | RX_LAST)) == (RX_FIRST | RX_LAST) &&
        Length >= 18 && Length <= MAX_FRAME + 4 && Accept (Nic, Frame))
    {
      Length -= 4; // DMA includes the Ethernet FCS.
      if (*Size < Length)
      {
        *Size = Length;
        Status = EFI_BUFFER_TOO_SMALL;
        goto Done;
      }
      CopyMem (Buffer, Frame, Length);
      *Size = Length;
      if (HeaderSize)
      {
        *HeaderSize = 14;
      }
      if (Source)
      {
        ZeroMem (Source, sizeof (*Source));
        CopyMem (Source, Frame + 6, 6);
      }
      if (Destination)
      {
        ZeroMem (Destination, sizeof (*Destination));
        CopyMem (Destination, Frame, 6);
      }
      if (Protocol)
      {
        *Protocol = ((UINT16)Frame[12] << 8) | Frame[13];
      }
      RearmRx (Nic, Nic->RxIndex);
      Nic->RxIndex = (Nic->RxIndex + 1) % RX_COUNT;
      GmacWrite (&Nic->Hardware, DMA_RX_POLL, 1);
      Status = EFI_SUCCESS;
      goto Done;
    }
    RearmRx (Nic, Nic->RxIndex);
    Nic->RxIndex = (Nic->RxIndex + 1) % RX_COUNT;
    GmacWrite (&Nic->Hardware, DMA_RX_POLL, 1);
  }
  Status = EFI_NOT_READY;
Done:
  EfiReleaseLock (&Nic->Lock);
  return Status;
}
STATIC EFI_STATUS
CreateNic (FDT_CLIENT_PROTOCOL *Fdt, INT32 Node)
{
  EFI_STATUS Status;
  EFI_PHYSICAL_ADDRESS Address = MAX_UINT32;
  GMAC_DEVICE *Nic = AllocateZeroPool (sizeof (*Nic));
  if (Nic == NULL)
  {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = GmacParseHardware (Fdt, Node, &Nic->Hardware);
  if (EFI_ERROR (Status))
  {
    FreePool (Nic);
    return Status;
  }
  Status = GmacPlatformInitialize (&Nic->Hardware);
  if (EFI_ERROR (Status))
  {
    FreePool (Nic);
    return Status;
  }
  if ((GmacRead (&Nic->Hardware, MAC_VERSION) & 255) != 0x37)
  {
    FreePool (Nic);
    return EFI_UNSUPPORTED;
  }
  Status = GmacPhyInitialize (&Nic->Hardware);
  if (EFI_ERROR (Status))
  {
    FreePool (Nic);
    return Status;
  }
  EfiInitializeLock (&Nic->Lock, TPL_CALLBACK);
  Nic->Snp = (EFI_SIMPLE_NETWORK_PROTOCOL){EFI_SIMPLE_NETWORK_PROTOCOL_REVISION,
                                           Start,
                                           Stop,
                                           Initialize,
                                           Reset,
                                           Shutdown,
                                           ReceiveFilters,
                                           StationAddress,
                                           Statistics,
                                           MCastIpToMac,
                                           NvData,
                                           GetStatus,
                                           Transmit,
                                           Receive,
                                           NULL,
                                           &Nic->Mode};
  Nic->Mode.State = EfiSimpleNetworkStopped;
  Nic->Mode.HwAddressSize = 6;
  Nic->Mode.MediaHeaderSize = 14;
  Nic->Mode.MaxPacketSize = 1500;
  Nic->Mode.ReceiveFilterMask = FILTER_MASK;
  Nic->Mode.MaxMCastFilterCount = 16;
  Nic->Mode.IfType = 1;
  Nic->Mode.MacAddressChangeable = TRUE;
  Nic->Mode.MultipleTxSupported = TRUE;
  Nic->Mode.MediaPresentSupported = TRUE;
  SetMem (&Nic->Mode.BroadcastAddress, sizeof (EFI_MAC_ADDRESS), 0xff);
  Status = GmacSelectAddress (Fdt, &Nic->Hardware,
                              Nic->Mode.PermanentAddress.Addr);
  if (!EFI_ERROR (Status))
    CopyMem (&Nic->Mode.CurrentAddress, &Nic->Mode.PermanentAddress,
             sizeof (EFI_MAC_ADDRESS));
  if (EFI_ERROR (Status))
  {
    FreePool (Nic);
    return Status;
  }
  Nic->CounterFrequency = GetPerformanceCounterProperties (NULL, NULL);
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData,
                               EFI_SIZE_TO_PAGES (sizeof (GMAC_DMA)), &Address);
  if (EFI_ERROR (Status))
  {
    FreePool (Nic);
    return Status;
  }
  Nic->Dma = (VOID *)(UINTN)Address;
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_CALLBACK, Poll, Nic, &Nic->Snp.WaitForPacket);
  if (EFI_ERROR (Status))
  {
    goto Failed;
  }
  Status =
      gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Poll, Nic, &Nic->PollEvent);
  if (EFI_ERROR (Status))
  {
    goto Failed;
  }
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBootServices, Nic,
                               &gEfiEventExitBootServicesGuid, &Nic->ExitEvent);
  if (EFI_ERROR (Status))
  {
    goto Failed;
  }
  Nic->Path.Mac.Header = (EFI_DEVICE_PATH_PROTOCOL){
      MESSAGING_DEVICE_PATH, MSG_MAC_ADDR_DP, {sizeof (MAC_ADDR_DEVICE_PATH), 0}};
  CopyMem (&Nic->Path.Mac.MacAddress, &Nic->Mode.PermanentAddress, sizeof (EFI_MAC_ADDRESS));
  Nic->Path.Mac.IfType = 1;
  Nic->Path.End =
      (EFI_DEVICE_PATH_PROTOCOL){END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}};
  InsertTailList (&Devices, &Nic->Link);
  Status = gBS->InstallMultipleProtocolInterfaces (&Nic->Handle, &gEfiSimpleNetworkProtocolGuid,
                                                   &Nic->Snp, &gEfiDevicePathProtocolGuid,
                                                   &Nic->Path, NULL);
  if (!EFI_ERROR (Status))
  {
    return EFI_SUCCESS;
  }
  RemoveEntryList (&Nic->Link);
Failed:
  if (Nic->ExitEvent)
  {
    gBS->CloseEvent (Nic->ExitEvent);
  }
  if (Nic->PollEvent)
  {
    gBS->CloseEvent (Nic->PollEvent);
  }
  if (Nic->Snp.WaitForPacket)
  {
    gBS->CloseEvent (Nic->Snp.WaitForPacket);
  }
  gBS->FreePages (Address, EFI_SIZE_TO_PAGES (sizeof (GMAC_DMA)));
  Nic->Dma = NULL;
  FreePool (Nic);
  return Status;
}

EFI_STATUS EFIAPI
GmacInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  FDT_CLIENT_PROTOCOL *Fdt;
  EFI_STATUS Status, Result = EFI_NOT_FOUND;
  INT32 Node = -1;
  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&Fdt);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  GmacPrepareAddresses (Fdt);
  STATIC TH1520_MAC_CONFIG_PROTOCOL Config = {
    GmacGetConfig, GmacWriteSettings
  };
  while (!EFI_ERROR (Fdt->FindNextCompatibleNode (Fdt, "thead,th1520-gmac", Node, &Node)))
  {
    Status = CreateNic (Fdt, Node);
    if (!EFI_ERROR (Status))
    {
      Result = EFI_SUCCESS;
    }
    else
    {
      DEBUG ((DEBUG_ERROR, "TH1520 GMAC node %d: %r\n", Node, Status));
    }
  }
  Status = gBS->InstallProtocolInterface (&ImageHandle,
      &gTh1520MacConfigProtocolGuid, EFI_NATIVE_INTERFACE, &Config);
  return EFI_ERROR (Status) ? Result : EFI_SUCCESS;
}
