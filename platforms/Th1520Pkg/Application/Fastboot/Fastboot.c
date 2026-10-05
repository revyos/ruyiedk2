// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Fastboot.h"
#include "Targets.h"
#include <Protocol/Th1520EmmcAccess.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_STATUS
Reply (CONST CHAR8 *Prefix, CONST CHAR8 *Message)
{
  CHAR8 Response[65];
  AsciiSPrint (Response, sizeof (Response), "%a%a", Prefix, Message);
  return UsbSend (Response, AsciiStrLen (Response));
}

STATIC UINT64
Capacity (TH1520_FLASH_TARGET Target, TH1520_EMMC_ACCESS_PROTOCOL *Emmc)
{
  return Th1520TargetCapacity (Target, Emmc ? Emmc->UserSize : 0,
                              Emmc ? Emmc->BootSize : 0);
}

STATIC EFI_STATUS
StorageIo (TH1520_STORAGE_RANGE *Range, TH1520_EMMC_ACCESS_PROTOCOL *Emmc,
           UINTN Length, VOID *Buffer, BOOLEAN Write)
{
  UINTN Partition;
  if (Range->Target == Th1520TargetSpi)
    return Write ? SpiFlash (Range->Offset, Buffer, Length) :
                   SpiRead (Range->Offset, Buffer, Length);
  if (Emmc == NULL) return EFI_NOT_FOUND;
  Partition = Range->Target == Th1520TargetEmmcBoot0 ? 1 :
              Range->Target == Th1520TargetEmmcBoot1 ? 2 : 0;
  return Write ? Emmc->Write (Emmc, Partition, Range->Offset, Length, Buffer) :
                 Emmc->Read (Emmc, Partition, Range->Offset, Length, Buffer);
}

EFI_STATUS EFIAPI
FastbootMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINT8 *Download, *Packet;
  UINTN Expected = 0, Done = 0, Size, I;
  BOOLEAN Complete = FALSE, Exit = FALSE;
  CHAR8 Command[65], Response[32];
  TH1520_EMMC_ACCESS_PROTOCOL *Emmc = NULL;
  gBS->LocateProtocol (&gTh1520EmmcAccessProtocolGuid, NULL, (VOID **)&Emmc);
  Status = gBS->SetWatchdogTimer (0, 0, 0, NULL);
  if (EFI_ERROR (Status))
  {
    return Status;
  }
  Download = AllocatePool (DOWNLOAD_MAX);
  Packet = AllocatePool (16385);
  if (!Download || !Packet)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }
  Status = UsbStart ();
  if (EFI_ERROR (Status))
  {
    UsbStop ();
    goto Out;
  }
  Print (L"USB fastboot: %a. ESC returns to the boot manager.\r\n", PcdGetPtr (PcdFirmwareName));
  Print (L"Storage targets: spi, emmc0, emmc0boot0, emmc0boot1\r\n");
  while (!Exit)
  {
    if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key)) && Key.ScanCode == SCAN_ESC)
    {
      break;
    }
    Size = Expected ? MIN (Expected - Done, 16384) : 512;
    Status = UsbReceive (Packet, &Size);
    if (Status == EFI_MEDIA_CHANGED)
    {
      Expected = Done = 0;
      Complete = FALSE;
      continue;
    }
    if (Status == EFI_NOT_READY)
    {
      MicroSecondDelay (100);
      continue;
    }
    if (EFI_ERROR (Status))
    {
      break;
    }
    if (Expected)
    {
      if (!Size || Size > Expected - Done)
      {
        Expected = 0;
        Complete = FALSE;
        Status = Reply ("FAIL", "invalid data size");
      }
      else
      {
        CopyMem (Download + Done, Packet, Size);
        Done += Size;
        if (Done == Expected)
        {
          Expected = 0;
          Complete = TRUE;
          Status = Reply ("OKAY", "");
        }
      }
      if (EFI_ERROR (Status))
      {
        break;
      }
      continue;
    }
    if (!Size || Size > 64 || ScanMem8 (Packet, Size, 0) != NULL)
    {
      Status = Reply ("FAIL", "invalid command");
      continue;
    }
    CopyMem (Command, Packet, Size);
    Command[Size] = 0;
    if (!AsciiStrnCmp (Command, "download:", 9))
    {
      Complete = FALSE;
      Done = 0;
      if (Size != 17)
      {
        Status = Reply ("FAIL", "invalid size");
        continue;
      }
      for (I = 9; I < 17; I++)
      {
        if (!((Command[I] >= '0' && Command[I] <= '9') ||
              (Command[I] >= 'a' && Command[I] <= 'f') || (Command[I] >= 'A' && Command[I] <= 'F')))
        {
          break;
        }
      }
      if (I != 17)
      {
        Status = Reply ("FAIL", "invalid size");
        continue;
      }
      Expected = AsciiStrHexToUintn (Command + 9);
      if (!Expected || Expected > DOWNLOAD_MAX)
      {
        Expected = 0;
        Status = Reply ("FAIL", "download too large");
        continue;
      }
      AsciiSPrint (Response, sizeof (Response), "DATA%08x", (UINT32)Expected);
      Status = UsbSend (Response, 12);
    }
    else if (!AsciiStrnCmp (Command, "getvar:", 7))
    {
      CONST CHAR8 *Name = Command + 7, *Value = NULL;
      TH1520_STORAGE_RANGE Range;
      UINT64 Bytes;
      if (!AsciiStrnCmp (Name, "partition-size:", 15) &&
          Th1520ParseStorageRange (Name + 15, &Range))
      {
        Bytes = Capacity (Range.Target, Emmc);
        if (Bytes && Range.Offset <= Bytes &&
            (!Range.HasLength || Range.Length <= Bytes - Range.Offset))
        {
          Bytes = Range.HasLength ? Range.Length : Bytes - Range.Offset;
          AsciiSPrint (Response, sizeof (Response), "0x%016Lx", Bytes);
          Value = Response;
        }
      }
      else if (!AsciiStrnCmp (Name, "partition-type:", 15) &&
               Th1520ParseStorageRange (Name + 15, &Range))
        Value = "raw";
      else if (!AsciiStrnCmp (Name, "has-slot:", 9) &&
               Th1520ParseStorageRange (Name + 9, &Range))
        Value = "no";
      else if (!AsciiStrCmp (Name, "version")) Value = "0.4";
      else if (!AsciiStrCmp (Name, "product") || !AsciiStrCmp (Name, "serialno"))
        Value = (CONST CHAR8 *)PcdGetPtr (PcdFastbootProduct);
      else if (!AsciiStrCmp (Name, "max-download-size") ||
               !AsciiStrCmp (Name, "max-fetch-size")) Value = "0x02000000";
      else if (!AsciiStrCmp (Name, "is-userspace")) Value = "no";
      else if (!AsciiStrCmp (Name, "all"))
      {
        STATIC CONST CHAR8 *Names[] = {"spi", "emmc0", "emmc0boot0", "emmc0boot1"};
        CHAR8 Info[61];
        AsciiSPrint (Info, sizeof (Info), "product: %a", PcdGetPtr (PcdFastbootProduct));
        Status = Reply ("INFO", Info);
        for (I = 0; !EFI_ERROR (Status) && I < ARRAY_SIZE (Names); I++)
        {
          Th1520ParseStorageRange (Names[I], &Range);
          AsciiSPrint (Info, sizeof (Info), "partition-size:%a: 0x%016Lx",
                       Names[I], Capacity (Range.Target, Emmc));
          Status = Reply ("INFO", Info);
        }
        if (EFI_ERROR (Status)) break;
        Value = "";
      }
      Status = Reply (Value ? "OKAY" : "FAIL", Value ? Value : "unknown variable");
    }
    else if (!AsciiStrnCmp (Command, "fetch:", 6))
    {
      TH1520_STORAGE_RANGE Range;
      UINT64 Limit, Length;
      UINTN Sent;
      Complete = FALSE;
      if (!Th1520ParseStorageRange (Command + 6, &Range))
      {
        Status = Reply ("FAIL", "unknown medium or invalid range");
        continue;
      }
      Limit = Capacity (Range.Target, Emmc);
      Length = Range.HasLength ? Range.Length :
               (Range.Offset <= Limit ? Limit - Range.Offset : 0);
      if (!Limit || !Length || Length > DOWNLOAD_MAX ||
          Range.Offset > Limit || Length > Limit - Range.Offset)
      {
        Status = Reply ("FAIL", "invalid read range");
        continue;
      }
      Status = StorageIo (&Range, Emmc, (UINTN)Length, Download, FALSE);
      if (EFI_ERROR (Status))
      {
        Status = Reply ("FAIL", "storage read failed");
        continue;
      }
      AsciiSPrint (Response, sizeof (Response), "DATA%08x", (UINT32)Length);
      Status = UsbSend (Response, 12);
      for (Sent = 0; !EFI_ERROR (Status) && Sent < Length; Sent += Size)
      {
        Size = MIN (16384, (UINTN)Length - Sent);
        Status = UsbSend (Download + Sent, Size);
      }
      if (!EFI_ERROR (Status)) Status = Reply ("OKAY", "");
    }
    else if (!AsciiStrnCmp (Command, "flash:", 6))
    {
      TH1520_STORAGE_RANGE Range;
      UINT64 Limit;
      if (!Th1520ParseStorageRange (Command + 6, &Range))
      {
        Complete = FALSE;
        Status = Reply ("FAIL", "unknown medium or invalid range");
        continue;
      }
      Limit = Capacity (Range.Target, Emmc);
      if (!Complete || !Done || Range.Offset > Limit || Done > Limit - Range.Offset ||
          (Range.HasLength && Range.Length != Done))
      {
        Complete = FALSE;
        Status = Reply ("FAIL", "invalid write range or no download");
        continue;
      }
      // Every payload is written verbatim, including sparse-container bytes.
      Complete = FALSE;
      Status = StorageIo (&Range, Emmc, Done, Download, TRUE);
      if (EFI_ERROR (Status))
      {
        CHAR8 Error[61];
        AsciiSPrint (Error, sizeof (Error), "storage write: %r", Status);
        Status = Reply ("FAIL", Error);
      }
      else Status = Reply ("OKAY", "");
    }
    else if (!AsciiStrCmp (Command, "continue"))
    {
      Status = Reply ("OKAY", "");
      Exit = TRUE;
    }
    else if (!AsciiStrCmp (Command, "reboot"))
    {
      Status = Reply ("OKAY", "");
      if (!EFI_ERROR (Status))
      {
        UsbStop ();
        gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      }
      Exit = TRUE;
    }
    else if (!AsciiStrCmp (Command, "boot"))
    {
      EFI_HANDLE Loaded = NULL;
      if (!Complete)
      {
        Status = Reply ("FAIL", "no downloaded EFI image");
      }
      else
      {
        Status = gBS->LoadImage (FALSE, ImageHandle, NULL, Download, Done, &Loaded);
        if (EFI_ERROR (Status))
        {
          Status = Reply ("FAIL", "not a loadable EFI image");
        }
        else
        {
          Status = Reply ("OKAY", "");
          if (!EFI_ERROR (Status))
          {
            UsbStop ();
            Status = gBS->StartImage (Loaded, NULL, NULL);
            Exit = TRUE;
          }
          gBS->UnloadImage (Loaded);
        }
      }
    }
    else
    {
      Status = Reply ("FAIL", "unsupported command");
    }
    if (EFI_ERROR (Status))
    {
      break;
    }
  }
  UsbStop ();
Out:
  if (Download)
  {
    FreePool (Download);
  }
  if (Packet)
  {
    FreePool (Packet);
  }
  return Status == EFI_NOT_READY ? EFI_SUCCESS : Status;
}
