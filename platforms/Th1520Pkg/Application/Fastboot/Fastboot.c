// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "Fastboot.h"
#include "Targets.h"
#include <Protocol/Th1520BootArea.h>
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
EFI_STATUS EFIAPI
FastbootMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINT8 *Download, *Packet;
  UINTN Expected = 0, Done = 0, Size, I;
  BOOLEAN Complete = FALSE, Exit = FALSE;
  CHAR8 Command[65], Response[16];
  TH1520_BOOT_AREA_PROTOCOL *BootArea = NULL;
  TH1520_BOOT_PARTITIONS_PROTOCOL *Partitions = NULL;
  TH1520_IMAGE_POLICY Policy = {
      (CONST CHAR8 *)PcdGetPtr (PcdFitConfiguration), (CONST CHAR8 *)PcdGetPtr (PcdBoardCompatible),
      PcdGetBool (PcdFirmwareOnEmmc) ? "emmc-boot0" : "spi-nor", PcdGet32 (PcdFirmwareImageLimit)};
  if (PcdGetBool (PcdFirmwareOnEmmc))
  {
    gBS->LocateProtocol (&gTh1520BootAreaProtocolGuid, NULL, (VOID **)&BootArea);
  }
  if (PcdGetBool (PcdRecoveryMode))
  {
    gBS->LocateProtocol (&gTh1520BootPartitionsProtocolGuid, NULL, (VOID **)&Partitions);
  }
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
  if (PcdGetBool (PcdRecoveryMode))
  {
    Print (L"Raw recovery targets: spi, emmcboot0, emmcboot1\r\n");
  }
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
      TH1520_FLASH_TARGET Target = Th1520TargetNone;
      if (PcdGetBool (PcdRecoveryMode) && !AsciiStrnCmp (Name, "partition-size:", 15))
      {
        Target = Th1520RecoveryTarget (Name + 15);
      }
      if (Target != Th1520TargetNone)
      {
        UINTN Limit = Th1520TargetMaximum (Target, Partitions ? Partitions->Size : 0);
        if (Limit)
        {
          AsciiSPrint (Response, sizeof (Response), "0x%08x", (UINT32)Limit);
          Value = Response;
        }
      }
      else if (PcdGetBool (PcdRecoveryMode) && !AsciiStrnCmp (Name, "partition-type:", 15) &&
               Th1520RecoveryTarget (Name + 15) != Th1520TargetNone)
      {
        Value = "raw";
      }
      else if (PcdGetBool (PcdRecoveryMode) && !AsciiStrnCmp (Name, "has-slot:", 9) &&
               Th1520RecoveryTarget (Name + 9) != Th1520TargetNone)
      {
        Value = "no";
      }
      else if (!AsciiStrCmp (Name, "version"))
      {
        Value = "0.4";
      }
      else if (!AsciiStrCmp (Name, "product"))
      {
        Value = (CONST CHAR8 *)PcdGetPtr (PcdFastbootProduct);
      }
      else if (!AsciiStrCmp (Name, "serialno"))
      {
        Value = (CONST CHAR8 *)PcdGetPtr (PcdFastbootProduct);
      }
      else if (!AsciiStrCmp (Name, "max-download-size"))
      {
        Value = "0x02000000";
      }
      else if (!AsciiStrCmp (Name, "partition-size:loader"))
      {
        AsciiSPrint (Response, sizeof (Response), "0x%08x", PcdGet32 (PcdFirmwareImageLimit));
        Value = Response;
      }
      else if (!AsciiStrCmp (Name, "max-fetch-size") && BootArea != NULL)
      {
        Value = "0x00400000";
      }
      else if (!AsciiStrCmp (Name, "partition-type:loader"))
      {
        Value = "raw";
      }
      else if (!AsciiStrCmp (Name, "is-userspace") || !AsciiStrCmp (Name, "has-slot:loader"))
      {
        Value = "no";
      }
      else if (!AsciiStrCmp (Name, "all"))
      {
        CHAR8 Info[61];
        AsciiSPrint (Info, sizeof (Info), "product: %a", PcdGetPtr (PcdFastbootProduct));
        Status = Reply ("INFO", Info);
        if (EFI_ERROR (Status))
        {
          break;
        }
        Status = Reply ("INFO", "max-download-size: 0x02000000");
        if (EFI_ERROR (Status))
        {
          break;
        }
        AsciiSPrint (Info, sizeof (Info), "partition-size:loader: 0x%08x",
                     PcdGet32 (PcdFirmwareImageLimit));
        Status = Reply ("INFO", Info);
        if (EFI_ERROR (Status))
        {
          break;
        }
        Value = "";
      }
      Status = Reply (Value ? "OKAY" : "FAIL", Value ? Value : "unknown variable");
    }
    else if (!AsciiStrnCmp (Command, "fetch:loader", 12))
    {
      UINTN Offset = 0, Length = BootArea == NULL ? 0 : (UINTN)BootArea->Size, Sent;
      CHAR8 *End;
      BOOLEAN Valid = Command[12] == 0;
      if (Command[12] == ':')
      {
        Status = AsciiStrHexToUintnS (Command + 13, &End, &Offset);
        if (!EFI_ERROR (Status) && *End == ':')
        {
          Status = AsciiStrHexToUintnS (End + 1, &End, &Length);
          Valid = !EFI_ERROR (Status) && *End == 0;
        }
      }
      Complete = FALSE;
      if (!Valid || BootArea == NULL || Length == 0 || Length > DOWNLOAD_MAX ||
          Offset > BootArea->Size || Length > BootArea->Size - Offset)
      {
        Status = Reply ("FAIL", "invalid boot0 read range");
        continue;
      }
      Status = BootArea->Read (BootArea, Offset, Length, Download);
      if (EFI_ERROR (Status))
      {
        Status = Reply ("FAIL", "boot0 read failed");
        continue;
      }
      AsciiSPrint (Response, sizeof (Response), "DATA%08x", (UINT32)Length);
      Status = UsbSend (Response, 12);
      for (Sent = 0; !EFI_ERROR (Status) && Sent < Length; Sent += Size)
      {
        Size = MIN (16384, Length - Sent);
        Status = UsbSend (Download + Sent, Size);
      }
      if (!EFI_ERROR (Status))
      {
        Status = Reply ("OKAY", "");
      }
    }
    else if (PcdGetBool (PcdRecoveryMode) && !AsciiStrnCmp (Command, "flash:", 6) &&
             Th1520RecoveryTarget (Command + 6) != Th1520TargetNone)
    {
      TH1520_FLASH_TARGET Target = Th1520RecoveryTarget (Command + 6);
      UINTN Limit = Th1520TargetMaximum (Target, Partitions ? Partitions->Size : 0);
      if (!Complete || !Th1520ValidRawImage (Download, Done, Limit))
      {
        Status = Reply ("FAIL", "invalid raw image, size or unavailable target");
      }
      else
      {
        Status = Target == Th1520TargetSpi ? SpiFlash (Download, Done) :
            Partitions->Write (Partitions,
                Target == Th1520TargetEmmcBoot0 ? 1 : 2, Download, Done);
        if (EFI_ERROR (Status))
        {
          CHAR8 Error[61];
          AsciiSPrint (Error, sizeof (Error), "%a: %r", Command + 6, Status);
          Status = Reply ("FAIL", Error);
        }
        else
        {
          Status = Reply ("OKAY", "");
        }
      }
    }
    else if (!AsciiStrCmp (Command, "flash:loader"))
    {
      if (!Complete || !Th1520ValidImage (Download, Done, &Policy))
      {
        Status = Reply ("FAIL", "invalid image for this board or storage");
      }
      else
      {
        Status =
            PcdGetBool (PcdFirmwareOnEmmc)
                ? (BootArea == NULL ? EFI_NOT_READY : BootArea->Write (BootArea, Download, Done))
                : SpiFlash (Download, Done);
        if (EFI_ERROR (Status))
        {
          CHAR8 Error[61];
          AsciiSPrint (Error, sizeof (Error), "%a: %r",
                       PcdGetBool (PcdFirmwareOnEmmc) ? "eMMC boot0" : "SPI", Status);
          Status = Reply ("FAIL", Error);
        }
        else
        {
          Status = Reply ("OKAY", "");
        }
      }
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
