// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only firmware throughput and repeat-read integrity check.
#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/UsbIo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI
IoTestMain (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE *Handles;
  UINTN Count, Index, Offset, Chunk = SIZE_128KB;
  UINT8 *Buffer, *Verify;
  EFI_STATUS Status, Result = EFI_SUCCESS;
  Buffer = AllocateAlignedPages (EFI_SIZE_TO_PAGES (Chunk), SIZE_4KB);
  Verify = AllocateAlignedPages (EFI_SIZE_TO_PAGES (Chunk), SIZE_4KB);
  if (Buffer == NULL || Verify == NULL)
  {
    if (Buffer != NULL)
    {
      FreeAlignedPages (Buffer, EFI_SIZE_TO_PAGES (Chunk));
    }
    if (Verify != NULL)
    {
      FreeAlignedPages (Verify, EFI_SIZE_TO_PAGES (Chunk));
    }
    return EFI_OUT_OF_RESOURCES;
  }
  // Fastboot role switching restarts hub enumeration asynchronously.
  for (Index = 0; Index < 30; Index++)
  {
    gBS->Stall (100000);
  }
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
      &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles)))
  {
    for (Index = 0; Index < Count; Index++)
    {
      EFI_USB_IO_PROTOCOL *Usb;
      EFI_USB_DEVICE_DESCRIPTOR Desc;
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
          &gEfiUsbIoProtocolGuid, (VOID **)&Usb)) &&
          !EFI_ERROR (Usb->UsbGetDeviceDescriptor (Usb, &Desc)))
      {
        Print (L"USB: %04x:%04x USB=%04x\n", Desc.IdVendor,
               Desc.IdProduct, Desc.BcdUSB);
      }
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }
    FreePool (Handles);
  }
  Print (L"IO benchmark: read only, 64 MiB, 128 KiB requests\n");
  Print (L"eMMC clock=%04x host=%02x mode=%04x caps=%08x\n",
         MmioRead16 (0xffe708002cULL), MmioRead8 (0xffe7080028ULL),
         MmioRead16 (0xffe708003eULL), MmioRead32 (0xffe7080040ULL));
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR (Status))
  {
    goto Done;
  }
  for (Index = 0; Index < Count; Index++)
  {
    EFI_BLOCK_IO_PROTOCOL *Io;
    EFI_DEVICE_PATH_PROTOCOL *Path;
    BOOLEAN Usb = FALSE;
    UINT64 Start, Elapsed, Total = 0;
    UINT32 Crc[512], Check;
    UINTN Pass;
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                 (VOID **)&Io);
    if (EFI_ERROR (Status) || !Io->Media->MediaPresent ||
        Io->Media->LogicalPartition || Io->Media->BlockSize == 0 ||
        Chunk % Io->Media->BlockSize ||
        Io->Media->LastBlock < SIZE_64MB / Io->Media->BlockSize)
    {
      continue;
    }
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
        &gEfiDevicePathProtocolGuid, (VOID **)&Path)))
    {
      while (Path->Type != END_DEVICE_PATH_TYPE)
      {
        if (Path->Type == MESSAGING_DEVICE_PATH &&
            Path->SubType == MSG_USB_DP)
        {
          Usb = TRUE;
        }
        Path = (VOID *)((UINT8 *)Path + ReadUnaligned16 (
                         (UINT16 *)Path->Length));
      }
    }
    Print (L"Disk %u (%s), block=%u\n", Index,
           Usb ? L"USB" : L"eMMC/other", Io->Media->BlockSize);
    for (Pass = 0; Pass < 2; Pass++)
    {
      Elapsed = 0;
      for (Offset = 0; Offset < SIZE_64MB; Offset += Chunk)
      {
        Start = GetPerformanceCounter ();
        Status = Io->ReadBlocks (Io, Io->Media->MediaId,
                    Offset / Io->Media->BlockSize, Chunk, Buffer);
        Elapsed += GetTimeInNanoSecond (GetPerformanceCounter () - Start);
        if (EFI_ERROR (Status))
        {
          break;
        }
        gBS->CalculateCrc32 (Buffer, Chunk, &Check);
        if (Pass == 0)
        {
          Crc[Offset / Chunk] = Check;
        }
        else if (Crc[Offset / Chunk] != Check)
        {
          Status = EFI_CRC_ERROR;
          break;
        }
      }
      Total = Offset;
      Print (L"pass %u: %Lu bytes, %Lu ms, %Lu KiB/s: %r\n", Pass,
             Total, DivU64x32 (Elapsed, 1000000),
             Elapsed ? DivU64x64Remainder (Total * 1000000000ULL,
                                           Elapsed * 1024, NULL) : 0,
             Status);
      if (EFI_ERROR (Status))
      {
        break;
      }
    }
    if (!EFI_ERROR (Status))
    {
      gBS->CalculateCrc32 (Crc, sizeof (Crc), &Check);
      Print (L"64 MiB chunk-CRC fingerprint: %08x\n", Check);
    }
    // Exercise a caller buffer that cannot be directly used by ADMA.
    if (!EFI_ERROR (Status))
    {
      Status = Io->ReadBlocks (Io, Io->Media->MediaId, 0,
                               Io->Media->BlockSize, Verify);
      if (!EFI_ERROR (Status) && Io->Media->IoAlign <= 1)
      {
        Status = Io->ReadBlocks (Io, Io->Media->MediaId, 0,
                                 Io->Media->BlockSize, Buffer + 1);
        if (!EFI_ERROR (Status) && CompareMem (Verify, Buffer + 1,
                                               Io->Media->BlockSize))
        {
          Status = EFI_CRC_ERROR;
        }
      }
      Print (L"small/unaligned read: %r\n", Status);
    }
    if (EFI_ERROR (Status))
    {
      Result = Status;
    }
  }
  Status = Result;
  FreePool (Handles);
Done:
  FreeAlignedPages (Verify, EFI_SIZE_TO_PAGES (Chunk));
  FreeAlignedPages (Buffer, EFI_SIZE_TO_PAGES (Chunk));
  Print (L"IO benchmark complete\n");
  return Status;
}
