// SPDX-License-Identifier: BSD-2-Clause-Patent
// TH1520 DWC MSHC eMMC, fixed 1.8 V I/O and cache-safe ADMA2.
#include <Uefi.h>
#include "Adma.h"
#include <Library/CacheMaintenanceLib.h>
#include <Protocol/FdtClient.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/Th1520BootArea.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
STATIC UINTN MMC;
STATIC UINT32 BusWidth, RateLimit;
STATIC EFI_BLOCK_IO_MEDIA Media;
STATIC UINT32 ClockRate;
STATIC UINT8 PartitionConfig;
STATIC UINT64 BootAreaSize;
STATIC EFI_LOCK ControllerLock;
STATIC BOOLEAN UserAreaSelected;
STATIC UINT8 *DmaBuffer;
STATIC EMMC_ADMA_DESCRIPTOR *DmaTable;
STATIC BOOLEAN DmaEnabled;
STATIC UINT32 BusRate;

STATIC EFI_STATUS
ParseHardware (VOID)
{
  FDT_CLIENT_PROTOCOL *Fdt;
  CONST VOID *Value;
  UINT32 Size;
  INT32 Node = -1;
  EFI_STATUS Status;
  STATIC CONST CHAR8 *HighSpeed[] = {
    "cap-mmc-highspeed", "mmc-ddr-1_8v",
    "mmc-hs200-1_8v", "mmc-hs400-1_8v"
  };
  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL,
                                (VOID **)&Fdt);
  if (EFI_ERROR (Status))
    return Status;
  while (!EFI_ERROR (Fdt->FindNextCompatibleNode (
      Fdt, "thead,th1520-dwcmshc", Node, &Node)))
  {
    Status = Fdt->GetNodeProperty (Fdt, Node, "status", &Value, &Size);
    if (!EFI_ERROR (Status) &&
        !((Size == 5 && !CompareMem (Value, "okay", 5)) ||
          (Size == 3 && !CompareMem (Value, "ok", 3))))
      continue;
    // Removable SD slots have a different command and voltage sequence.
    if (EFI_ERROR (Fdt->GetNodeProperty (
        Fdt, Node, "non-removable", &Value, &Size)))
      continue;
    Status = Fdt->GetNodeProperty (Fdt, Node, "reg", &Value, &Size);
    if (EFI_ERROR (Status) || Size != 16)
      return EFI_COMPROMISED_DATA;
    MMC = SwapBytes64 (ReadUnaligned64 (Value));
    if (MMC != 0xffe7080000ULL ||
        SwapBytes64 (ReadUnaligned64 ((UINT8 *)Value + 8)) < 0x530)
      return EFI_UNSUPPORTED;
    BusWidth = 1;
    Status = Fdt->GetNodeProperty (Fdt, Node, "bus-width", &Value, &Size);
    if (!EFI_ERROR (Status))
    {
      if (Size != 4)
        return EFI_COMPROMISED_DATA;
      BusWidth = SwapBytes32 (ReadUnaligned32 (Value));
    }
    if (BusWidth != 1 && BusWidth != 4 && BusWidth != 8)
      return EFI_UNSUPPORTED;
    if (!EFI_ERROR (Fdt->GetNodeProperty (
        Fdt, Node, "no-1-8-v", &Value, &Size)))
      return EFI_UNSUPPORTED;
    RateLimit = 25000000;
    for (UINTN I = 0; I < ARRAY_SIZE (HighSpeed); I++)
      if (!EFI_ERROR (Fdt->GetNodeProperty (
          Fdt, Node, HighSpeed[I], &Value, &Size)))
      {
        // HS200/HS400 descriptions also allow the HS52 transition.
        RateLimit = 52000000;
        break;
      }
    Status = Fdt->GetNodeProperty (Fdt, Node, "max-frequency",
                                  &Value, &Size);
    if (!EFI_ERROR (Status))
    {
      if (Size != 4)
        return EFI_COMPROMISED_DATA;
      RateLimit = MIN (RateLimit, SwapBytes32 (ReadUnaligned32 (Value)));
      if (RateLimit < 400000)
        return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS
PrepareDma (VOID *Data, UINTN Blocks, BOOLEAN Write)
{
  EFI_STATUS Status;
  UINTN Bytes = Blocks * 512;
  Status = EmmcBuildAdma (DmaTable, EMMC_ADMA_COUNT,
                          (UINTN)DmaBuffer, Bytes);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (Write)
  {
    CopyMem (DmaBuffer, Data, Bytes);
  }
  // Dedicated pages avoid touching adjacent caller-owned cache lines.
  WriteBackInvalidateDataCacheRange (DmaBuffer, Bytes);
  WriteBackDataCacheRange (DmaTable,
                           sizeof (*DmaTable) * EMMC_ADMA_COUNT);
  MmioWrite32 (MMC + 0x58, (UINT32)(UINTN)DmaTable);
  MmioWrite32 (MMC + 0x5c, 0);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS
WaitInterrupt (UINT32 Mask)
{
  UINTN I;
  UINT32 Value;
  for (I = 0; I < 200000; I++)
  {
    Value = MmioRead32 (MMC + 0x30);
    if (Value & 0xffff8000U)
    {
      return EFI_DEVICE_ERROR;
    }
    if (Value & Mask)
    {
      MmioWrite32 (MMC + 0x30, Mask);
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}
STATIC EFI_STATUS
Command (UINTN Index, UINT32 Argument, UINT16 Flags, VOID *Data, UINTN Blocks, BOOLEAN Write)
{
  UINTN I, Block;
  UINT32 Word;
  EFI_STATUS Status;
  BOOLEAN Dma = Data != NULL && DmaEnabled;
  if (Data != NULL && (Blocks == 0 || Blocks > EMMC_DMA_SIZE / 512))
  {
    return EFI_BAD_BUFFER_SIZE;
  }
  for (I = 0; I < 200000 && (MmioRead32 (MMC + 0x24) & 3); I++)
  {
    MicroSecondDelay (10);
  }
  if (I == 200000)
  {
    return EFI_TIMEOUT;
  }
  MmioWrite32 (MMC + 0x30, MAX_UINT32);
  if (Data)
  {
    if (Dma)
    {
      Status = PrepareDma (Data, Blocks, Write);
      if (EFI_ERROR (Status))
      {
        return Status;
      }
    }
    MmioWrite16 (MMC + 4, 512);
    MmioWrite16 (MMC + 6, (UINT16)Blocks);
    MmioWrite16 (MMC + 0xc, (Dma ? BIT0 : 0) | (Write ? 0 : BIT4) |
                     (Blocks > 1 ? BIT1 | BIT2 | BIT5 : 0));
    Flags |= BIT5;
  }
  else
  {
    MmioWrite16 (MMC + 0xc, 0);
  }
  MmioWrite32 (MMC + 8, Argument);
  MmioWrite16 (MMC + 0xe, (UINT16)(Index << 8) | Flags);
  Status = WaitInterrupt (BIT0);
  if (EFI_ERROR (Status))
  {
    goto Error;
  }
  if (Flags == 0x1a || Flags == 0x1b || (Flags & BIT5))
  {
    if (MmioRead32 (MMC + 0x10) & 0xfdffa080U)
    {
      Status = EFI_DEVICE_ERROR;
      goto Error;
    }
  }
  if (Data && !Dma)
  {
    for (Block = 0; Block < Blocks; Block++)
    {
      Status = WaitInterrupt (Write ? BIT4 : BIT5);
      if (EFI_ERROR (Status))
      {
        goto Error;
      }
      for (I = 0; I < 128; I++)
      {
        if (Write)
        {
          CopyMem (&Word, (UINT8 *)Data + Block * 512 + I * 4, 4);
          MmioWrite32 (MMC + 0x20, Word);
        }
        else
        {
          Word = MmioRead32 (MMC + 0x20);
          CopyMem ((UINT8 *)Data + Block * 512 + I * 4, &Word, 4);
        }
      }
    }
  }
  if (Data || (Flags & 3) == 3)
  {
    Status = WaitInterrupt (BIT1);
    if (EFI_ERROR (Status))
    {
      goto Error;
    }
  }
  if (Dma && !Write)
  {
    InvalidateDataCacheRange (DmaBuffer, Blocks * 512);
    CopyMem (Data, DmaBuffer, Blocks * 512);
  }
  return EFI_SUCCESS;
Error:
  DEBUG (
      (DEBUG_ERROR, "Meles eMMC CMD%u: %r, status=%08x\n", Index, Status, MmioRead32 (MMC + 0x30)));
  MmioWrite8 (MMC + 0x2f, 6);
  for (I = 0; I < 10000 && MmioRead8 (MMC + 0x2f); I++)
  {
    MicroSecondDelay (10);
  }
  if (I == 10000)
  {
    // Do not reuse DMA pages after an unacknowledged controller reset.
    DmaEnabled = FALSE;
    Media.MediaPresent = FALSE;
  }
  return Status;
}
STATIC EFI_STATUS
Clock (UINT32 Rate)
{
  UINT32 Div;
  UINTN N;
  MmioWrite16 (MMC + 0x2c, 0);
  Div = (ClockRate + Rate - 1) / Rate;
  Div = (Div + 1) & ~1U;
  if (Div > 2046)
  {
    return EFI_UNSUPPORTED;
  }
  BusRate = ClockRate / Div;
  Div /= 2;
  MmioWrite16 (MMC + 0x2c, (UINT16)(((Div & 255) << 8) | ((Div & 0x300) >> 2) | 1));
  for (N = 0; N < 10000; N++)
  {
    if (MmioRead16 (MMC + 0x2c) & 2)
    {
      MmioOr16 (MMC + 0x2c, 4);
      MicroSecondDelay (1000);
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}
STATIC EFI_STATUS
Initialize (VOID)
{
  UINT32 Pll, Cfg, Div, Ocr;
  UINT64 Rate;
  UINTN N;
  EFI_STATUS Status;
  UINT8 Ext[512];
  UserAreaSelected = FALSE;
  Media.MediaPresent = FALSE;
  DmaEnabled = FALSE;
  MmioOr32 (0xffef010204ULL, BIT30);
  // PIO works without this policy, but the OS needs DDR access for ADMA.
  MmioWrite32 (0xfffc0280c0ULL, MAX_UINT32);
  Pll = MmioRead32 (0xffef010030ULL);
  Cfg = MmioRead32 (0xffef010034ULL);
  Div = (Pll & 63) * ((Pll >> 20) & 7) * ((Pll >> 24) & 7);
  if (!Div)
  {
    return EFI_DEVICE_ERROR;
  }
  Rate = LShiftU64 ((Pll >> 8) & 4095, 24);
  if (!(Cfg & BIT24))
  {
    Rate += Cfg & 0xffffff;
  }
  ClockRate = (Cfg & BIT30)
                  ? 6000000
                  : (UINT32)DivU64x32 (RShiftU64 (MultU64x32 (Rate, 24000000), 24), Div * 4);
  MmioWrite8 (MMC + 0x2f, 1);
  for (N = 0; N < 10000 && MmioRead8 (MMC + 0x2f); N++)
  {
    MicroSecondDelay (10);
  }
  if (N == 10000)
  {
    return EFI_TIMEOUT;
  }
  MmioWrite32 (MMC + 0x300, 0x00cc0001);
  MmioWrite8 (MMC + 0x31d, 0x10);
  MmioWrite8 (MMC + 0x31e, 0x32);
  MmioWrite8 (MMC + 0x326, 0x0a);
  MmioWrite8 (MMC + 0x31d, 0);
  MmioWrite16 (MMC + 0x304, 0x669);
  MmioWrite16 (MMC + 0x306, 0x669);
  MmioWrite16 (MMC + 0x30c, 0x669);
  MmioWrite16 (MMC + 0x308, 0x660);
  MmioWrite16 (MMC + 0x30a, 0x671);
  MmioWrite8 (MMC + 0x328, 0x60);
  MmioWrite8 (MMC + 0x324, 1);
  MmioWrite8 (MMC + 0x325, 0x25);
  MmioOr8 (MMC + 0x52c, 1);
  MmioWrite16 (MMC + 0x3e, BIT3);
  MmioWrite8 (MMC + 0x29, 0x0b);
  MmioWrite8 (MMC + 0x28, 0);
  MmioWrite8 (MMC + 0x2e, 0x0e);
  MmioWrite32 (MMC + 0x34, 0xffffffff);
  MmioWrite32 (MMC + 0x38, 0);
  Status = Clock (400000);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (0, 0, 0, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  for (N = 0; N < 1000; N++)
  {
    Status = Command (1, 0x40ff8080, 2, NULL, 0, FALSE);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Ocr = MmioRead32 (MMC + 0x10);
    if (Ocr & BIT31)
    {
      break;
    }
    MicroSecondDelay (1000);
  }
  if (N == 1000)
  {
    return EFI_TIMEOUT;
  }
  if (!(Ocr & BIT30))
  {
    return EFI_UNSUPPORTED;
  }
  Status = Command (2, 0, 9, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (3, 1 << 16, 0x1a, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (9, 1 << 16, 9, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (7, 1 << 16, 0x1b, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (8, 0, 0x1a, Ext, 1, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  PartitionConfig = Ext[179] & ~7U;
  BootAreaSize = (UINT64)Ext[226] * 128 * 1024;
  Media.LastBlock = ReadUnaligned32 ((CONST UINT32 *)(Ext + 212));
  if (!Media.LastBlock)
  {
    return EFI_UNSUPPORTED;
  }
  Media.LastBlock--;
  // Disable the optional volatile write cache so BlockIo writes are durable.
  if (Ext[33])
  {
    Status = Command (6, 0x03200100, 0x1b, NULL, 0, FALSE);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Status = Command (6, 0x03210000, 0x1b, NULL, 0, FALSE);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
  }
  // Select the user area while preserving the boot-partition configuration.
  Status = Command (6, 0x03b30000 | ((Ext[179] & ~7U) << 8), 0x1b, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = Command (6, 0x03b70000 |
      ((BusWidth == 8 ? 2U : BusWidth == 4 ? 1U : 0U) << 8),
      0x1b, NULL, 0, FALSE);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  MmioWrite8 (MMC + 0x28,
                 BusWidth == 8 ? BIT5 : BusWidth == 4 ? BIT1 : 0);
  Div = RateLimit;
  if (Div > 25000000 && (Ext[196] & 3))
  {
    Div = MIN (Div, (Ext[196] & BIT1) ? 52000000 : 26000000);
    Status = Command (6, 0x03b90100, 0x1b, NULL, 0, FALSE);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    MmioOr8 (MMC + 0x28, BIT2);
    // TH1520 DWC MSHC maps MMC HS52 to SDR50, as in snps_sdhci.c.
    MmioAndThenOr16 (MMC + 0x3e, (UINT16)~7U, 2);
  }
  else
  {
    Div = MIN (Div, 25000000);
  }
  if (Div == 0)
  {
    return EFI_UNSUPPORTED;
  }
  Status = Clock (Div);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  if (DmaTable != NULL && (MmioRead32 (MMC + 0x40) & BIT19))
  {
    MmioAndThenOr8 (MMC + 0x28, (UINT8)~(BIT3 | BIT4), BIT4);
    DmaEnabled = TRUE;
  }
  Media.MediaId = 1;
  Media.MediaPresent = TRUE;
  Media.BlockSize = 512;
  Media.IoAlign = 1;
  UserAreaSelected = TRUE;
  Print (L"TH1520 eMMC: %u kHz, %u-bit %s\n", BusRate / 1000,
         BusWidth,
         DmaEnabled ? L"ADMA2" : L"PIO");
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
Reset (EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN Extended)
{
  EFI_STATUS Status;
  EfiAcquireLock (&ControllerLock);
  Status = Initialize ();
  EfiReleaseLock (&ControllerLock);
  return Status;
}
STATIC EFI_STATUS
Transfer (UINT32 MediaId, EFI_LBA Lba, UINTN Size, VOID *Buffer, BOOLEAN Write)
{
  UINTN Blocks, Count;
  EFI_STATUS Status;
  if (!Media.MediaPresent)
  {
    return EFI_NO_MEDIA;
  }
  if (MediaId != Media.MediaId)
  {
    return EFI_MEDIA_CHANGED;
  }
  if (!Size)
  {
    return EFI_SUCCESS;
  }
  if (!Buffer)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (Size % 512)
  {
    return EFI_BAD_BUFFER_SIZE;
  }
  Blocks = Size / 512;
  if (Lba > Media.LastBlock || Blocks - 1 > Media.LastBlock - Lba)
  {
    return EFI_INVALID_PARAMETER;
  }
  while (Blocks)
  {
    Count = MIN (Blocks, DmaEnabled ? EMMC_DMA_SIZE / 512 : 128);
    Status = Command (Write ? (Count == 1 ? 24 : 25) : (Count == 1 ? 17 : 18), (UINT32)Lba, 0x1a,
                      Buffer, Count, Write);
    if (EFI_ERROR (Status))
    {
      return Status;
    }
    Buffer = (UINT8 *)Buffer + Count * 512;
    Lba += Count;
    Blocks -= Count;
  }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI
ReadBlocks (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *Buffer)
{
  EFI_STATUS Status;
  EfiAcquireLock (&ControllerLock);
  Status = UserAreaSelected ? Transfer (Id, Lba, Size, Buffer, FALSE) : EFI_DEVICE_ERROR;
  EfiReleaseLock (&ControllerLock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
WriteBlocks (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *Buffer)
{
  EFI_STATUS Status;
  EfiAcquireLock (&ControllerLock);
  Status = UserAreaSelected ? Transfer (Id, Lba, Size, Buffer, TRUE) : EFI_DEVICE_ERROR;
  EfiReleaseLock (&ControllerLock);
  return Status;
}
STATIC EFI_STATUS EFIAPI
Flush (EFI_BLOCK_IO_PROTOCOL *This)
{
  return EFI_SUCCESS;
}
STATIC EFI_BLOCK_IO_PROTOCOL BlockIo = {
    EFI_BLOCK_IO_PROTOCOL_REVISION, &Media, Reset, ReadBlocks, WriteBlocks, Flush};
#pragma pack(1)
STATIC struct
{
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} Path = {{{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
           {0xcdb1593b, 0x1ca2, 0x4bd4, {0xb5, 0xf5, 0xce, 0xb8, 0xdb, 0x9c, 0x57, 0x47}}},
          {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}};
#pragma pack()
STATIC EFI_STATUS
SelectPartition (UINT8 Partition)
{
  EFI_STATUS Status;
  UserAreaSelected = FALSE;
  Status = Command (6, 0x03b30000 | ((UINT32)(PartitionConfig | Partition) << 8), 0x1b, NULL, 0,
                    FALSE);
  // A failed restore must never redirect ordinary BlockIo writes into boot0.
  UserAreaSelected = !EFI_ERROR (Status) && Partition == 0;
  return Status;
}

STATIC EFI_STATUS EFIAPI
BootPartitionRead (UINTN Partition, UINT64 Limit, UINTN Offset, UINTN Length, VOID *Buffer)
{
  EFI_STATUS Status, Restore;
  if (Partition < 1 || Partition > 2 || Limit > BootAreaSize || Buffer == NULL ||
      Offset % 512 || Length % 512 || Offset > Limit || Length > Limit - Offset)
  {
    return EFI_INVALID_PARAMETER;
  }
  if (!Length)
  {
    return EFI_SUCCESS;
  }
  EfiAcquireLock (&ControllerLock);
  Status = SelectPartition ((UINT8)Partition);
  if (!EFI_ERROR (Status))
  {
    Status = Transfer (Media.MediaId, Offset / 512, Length, Buffer, FALSE);
  }
  Restore = SelectPartition (0);
  EfiReleaseLock (&ControllerLock);
  return EFI_ERROR (Status) ? Status : Restore;
}

STATIC EFI_STATUS EFIAPI
BootPartitionWrite (UINTN Partition, UINT64 Limit, CONST VOID *Buffer, UINTN Length)
{
  EFI_STATUS Status = EFI_SUCCESS, Restore;
  UINT8 *Block, *Verify;
  UINTN Offset, Count, Padded;
  if (Partition < 1 || Partition > 2 || Limit > BootAreaSize || Buffer == NULL ||
      Length == 0 || Length > Limit || ALIGN_VALUE (Length, 512) > Limit)
  {
    return EFI_INVALID_PARAMETER;
  }
  Block = AllocatePool (SIZE_64KB);
  Verify = AllocatePool (SIZE_64KB);
  if (Block == NULL || Verify == NULL)
  {
    if (Block != NULL)
    {
      FreePool (Block);
    }
    if (Verify != NULL)
    {
      FreePool (Verify);
    }
    return EFI_OUT_OF_RESOURCES;
  }
  EfiAcquireLock (&ControllerLock);
  Status = SelectPartition ((UINT8)Partition);
  for (Offset = 0; !EFI_ERROR (Status) && Offset < Length; Offset += Count)
  {
    Count = MIN (SIZE_64KB, Length - Offset);
    Padded = ALIGN_VALUE (Count, 512);
    SetMem (Block, Padded, 0xff);
    CopyMem (Block, (CONST UINT8 *)Buffer + Offset, Count);
    Status = Transfer (Media.MediaId, Offset / 512, Padded, Block, TRUE);
    if (!EFI_ERROR (Status))
    {
      Status = Transfer (Media.MediaId, Offset / 512, Padded, Verify, FALSE);
    }
    if (!EFI_ERROR (Status) && CompareMem (Block, Verify, Padded))
    {
      Status = EFI_DEVICE_ERROR;
    }
  }
  Restore = SelectPartition (0);
  EfiReleaseLock (&ControllerLock);
  FreePool (Verify);
  FreePool (Block);
  return EFI_ERROR (Status) ? Status : Restore;
}

STATIC EFI_STATUS EFIAPI
BootAreaRead (TH1520_BOOT_AREA_PROTOCOL *This, UINTN Offset, UINTN Length, VOID *Buffer)
{
  return This == NULL ? EFI_INVALID_PARAMETER :
      BootPartitionRead (1, This->Size, Offset, Length, Buffer);
}

STATIC EFI_STATUS EFIAPI
BootAreaWrite (TH1520_BOOT_AREA_PROTOCOL *This, CONST VOID *Buffer, UINTN Length)
{
  return This == NULL ? EFI_INVALID_PARAMETER :
      BootPartitionWrite (1, This->Size, Buffer, Length);
}

STATIC EFI_STATUS EFIAPI
BootPartitionsRead (TH1520_BOOT_PARTITIONS_PROTOCOL *This, UINTN Partition,
                    UINTN Offset, UINTN Length, VOID *Buffer)
{
  return This == NULL ? EFI_INVALID_PARAMETER :
      BootPartitionRead (Partition, BootAreaSize, Offset, Length, Buffer);
}

STATIC EFI_STATUS EFIAPI
BootPartitionsWrite (TH1520_BOOT_PARTITIONS_PROTOCOL *This, UINTN Partition,
                     CONST VOID *Buffer, UINTN Length)
{
  return This == NULL ? EFI_INVALID_PARAMETER :
      BootPartitionWrite (Partition, BootAreaSize, Buffer, Length);
}

STATIC TH1520_BOOT_PARTITIONS_PROTOCOL BootPartitions = {
    0, BootPartitionsRead, BootPartitionsWrite};

STATIC TH1520_BOOT_AREA_PROTOCOL BootArea = {0, BootAreaRead, BootAreaWrite};

EFI_STATUS EFIAPI
EmmcInitialize (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  EFI_PHYSICAL_ADDRESS Address = MAX_UINT32;
  Status = ParseHardware ();
  if (EFI_ERROR (Status))
    return Status;
  // 32-bit ADMA works independently of SDHCI v4/64-bit mode selection.
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData,
      EFI_SIZE_TO_PAGES (EMMC_DMA_SIZE) + 1, &Address);
  if (!EFI_ERROR (Status))
  {
    DmaTable = (VOID *)(UINTN)Address;
    DmaBuffer = (VOID *)(UINTN)(Address + EFI_PAGE_SIZE);
  }
  EfiInitializeLock (&ControllerLock, TPL_NOTIFY);
  Status = Initialize ();
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gEfiBlockIoProtocolGuid, &BlockIo,
                                                   &gEfiDevicePathProtocolGuid, &Path, NULL);
  if (!EFI_ERROR (Status) && PcdGetBool (PcdFirmwareOnEmmc))
  {
    if (BootAreaSize < PcdGet32 (PcdFirmwareImageLimit))
    {
      return EFI_UNSUPPORTED;
    }
    BootArea.Size = PcdGet32 (PcdFirmwareImageLimit);
    Status = gBS->InstallProtocolInterface (&Handle, &gTh1520BootAreaProtocolGuid,
                                            EFI_NATIVE_INTERFACE, &BootArea);
  }
  if (!EFI_ERROR (Status) && PcdGetBool (PcdRecoveryMode) && BootAreaSize != 0)
  {
    BootPartitions.Size = BootAreaSize;
    Status = gBS->InstallProtocolInterface (&Handle, &gTh1520BootPartitionsProtocolGuid,
                                            EFI_NATIVE_INTERFACE, &BootPartitions);
  }
  return Status;
}
