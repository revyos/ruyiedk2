// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exercise SNP and DMA through the DWMAC internal loopback path.
#include <Uefi.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FdtClient.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI
NetTestMain (EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SIMPLE_NETWORK_PROTOCOL *Snp;
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN Count, Index, Frame, Byte, Size, Slot;
  UINT8 Tx[8][1514], Rx[2048];
  UINT16 Protocol = 0x88b5;
  VOID *Cookie;
  UINT64 Start, PacketStart, Elapsed;
  UINT32 Saved;
  UINTN Base = 0xffe7070000ULL;
  EFI_TPL Tpl;
  BOOLEAN Tested = FALSE;
  FDT_CLIENT_PROTOCOL *Fdt;
  CONST VOID *Compatible;
  UINT32 CompatibleSize;
  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL,
                                 (VOID **)&Fdt);
  if (EFI_ERROR (Status) || EFI_ERROR (Fdt->GetNodeProperty (
      Fdt, 0, "compatible", &Compatible, &CompatibleSize)) ||
      CompatibleSize < sizeof ("milkv,meles") ||
      CompareMem (Compatible, "milkv,meles", sizeof ("milkv,meles")))
  {
    return EFI_UNSUPPORTED;
  }
  Status = gBS->LocateHandleBuffer (ByProtocol,
      &gEfiSimpleNetworkProtocolGuid, NULL, &Count, &Handles);
  Print (L"SNP locate: %r, count=%u\n", Status, EFI_ERROR (Status) ? 0 : Count);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  for (Index = 0; Index < Count; Index++)
  {
    Status = gBS->HandleProtocol (Handles[Index],
        &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp);
    if (EFI_ERROR (Status))
    {
      continue;
    }
    EFI_DEVICE_PATH_PROTOCOL *Path;
    // PXE children publish aliases of the same SNP. Use the MAC-only
    // physical handle so disconnecting its consumers leaves SNP alive.
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
        &gEfiDevicePathProtocolGuid, (VOID **)&Path)) ||
        Path->Type != MESSAGING_DEVICE_PATH ||
        Path->SubType != MSG_MAC_ADDR_DP ||
        ((EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Path +
          sizeof (MAC_ADDR_DEVICE_PATH)))->Type != END_DEVICE_PATH_TYPE)
    {
      continue;
    }
    Print (L"SNP %u: state=%u\n", Index, Snp->Mode->State);
    gBS->DisconnectController (Handles[Index], NULL, NULL);
    if (Snp->Mode->State == EfiSimpleNetworkStopped)
    {
      Snp->Start (Snp);
    }
    if (Snp->Mode->State == EfiSimpleNetworkInitialized)
    {
      Status = Snp->Shutdown (Snp);
      Print (L"SNP shutdown: %r\n", Status);
    }
    Status = Snp->Initialize (Snp, 0, 0);
    Print (L"SNP initialize: %r\n", Status);
    if (EFI_ERROR (Status))
    {
      break;
    }
    Snp->ReceiveFilters (Snp, EFI_SIMPLE_NETWORK_RECEIVE_UNICAST,
                         0, FALSE, 0, NULL);
    Tested = TRUE;
    Saved = MmioRead32 (Base);
    Print (L"SNP loopback: MAC=%08x, RGMII divider=%08x\n", Saved,
           MmioRead32 (0xffec00300cULL));
    // Keep other SNP consumers out of this bounded diagnostic session.
    Tpl = gBS->RaiseTPL (TPL_CALLBACK);
    MmioWrite32 (Base, Saved | BIT12);
    Start = GetPerformanceCounter ();
    for (Frame = 0; Frame < 16384; Frame += 8)
    {
      for (Slot = 0; Slot < 8; Slot++)
      {
        for (Byte = 14; Byte < sizeof (Tx[0]); Byte++)
        {
          Tx[Slot][Byte] = (UINT8)(Byte + Frame + Slot);
        }
        Status = Snp->Transmit (Snp, 14, sizeof (Tx[0]), Tx[Slot], NULL,
                                &Snp->Mode->CurrentAddress, &Protocol);
        if (EFI_ERROR (Status))
        {
          break;
        }
      }
      if (EFI_ERROR (Status))
      {
        break;
      }
      PacketStart = GetPerformanceCounter ();
      for (Slot = 0; Slot < 8; Slot++)
      {
        do
        {
          Cookie = NULL;
          Status = Snp->GetStatus (Snp, NULL, &Cookie);
        } while (!EFI_ERROR (Status) && Cookie == NULL &&
            GetTimeInNanoSecond (GetPerformanceCounter () - PacketStart) <
            100000000);
        if (EFI_ERROR (Status) || Cookie != Tx[Slot])
        {
          Status = EFI_TIMEOUT;
          break;
        }
      }
      if (EFI_ERROR (Status))
      {
        break;
      }
      for (Slot = 0; Slot < 8; Slot++)
      {
        do
        {
          Size = sizeof (Rx);
          Status = Snp->Receive (Snp, NULL, &Size, Rx, NULL, NULL, NULL);
        } while (Status == EFI_NOT_READY &&
            GetTimeInNanoSecond (GetPerformanceCounter () - PacketStart) <
            100000000);
        if (EFI_ERROR (Status))
        {
          break;
        }
        if (Size != sizeof (Tx[0]) || CompareMem (Tx[Slot], Rx, Size))
        {
          Status = EFI_CRC_ERROR;
          break;
        }
      }
      if (EFI_ERROR (Status))
      {
        break;
      }
    }
    Elapsed = GetTimeInNanoSecond (GetPerformanceCounter () - Start);
    MmioWrite32 (Base, Saved);
    // Retire any outstanding caller buffers before this image returns.
    EFI_STATUS Cleanup = Snp->Shutdown (Snp);
    gBS->RestoreTPL (Tpl);
    if (EFI_ERROR (Cleanup))
    {
      Status = Cleanup;
    }
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    Print (L"SNP loopback: %u verified frames, %Lu ms, %Lu KiB/s: %r\n",
           Frame, DivU64x32 (Elapsed, 1000000),
           Elapsed ? DivU64x64Remainder (
               (UINT64)Frame * sizeof (Tx[0]) * 1000000000,
               Elapsed * 1024, NULL) : 0, Status);
    break;
  }
  FreePool (Handles);
  return !Tested && !EFI_ERROR (Status) ? EFI_NOT_FOUND : Status;
}
