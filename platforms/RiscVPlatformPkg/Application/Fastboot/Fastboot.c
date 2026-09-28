// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FastbootPlatformLib.h>
#include <Library/FastbootTransportLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define PACKET_BYTES SIZE_16KB
#define DOWNLOAD_TIMEOUT_NS 5000000000ULL

STATIC EFI_STATUS
Reply (CONST CHAR8 *Prefix, CONST CHAR8 *Value)
{
  CHAR8 Response[65];
  AsciiSPrint (Response, sizeof (Response), "%a%a", Prefix, Value);
  return FastbootUsbSend (Response, AsciiStrLen (Response));
}

STATIC UINT64 mLastProgress;
STATIC CONST CHAR8 *mFlashTarget;
STATIC BOOLEAN mProgressNow, mVerify;

STATIC EFI_STATUS
FlashProgress (UINT64 Done, UINT64 Total, BOOLEAN Verify)
{
  EFI_INPUT_KEY Key;
  CHAR8 Value[60], SerialLine[128];
  UINT64 Now;
  EFI_STATUS Status = FastbootUsbPoll ();

  if (EFI_ERROR (Status))
    return Status;
  if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))
      && Key.ScanCode == SCAN_ESC)
    return EFI_ABORTED;
  Now = GetPerformanceCounter ();
  if (mProgressNow || mVerify != Verify || Done == Total
      || GetTimeInNanoSecond (Now - mLastProgress) >= 1000000000ULL)
  {
    UINT32 Percent = Done == Total ? 100 : (UINT32)MIN (99,
        (Done >> 10) * 100 / MAX (Total >> 10, 1));
    UINT64 TotalMib = (Total >> 20) + ((Total & (SIZE_1MB - 1)) != 0);
    UINT64 DoneMib = Done == Total ? TotalMib : Done >> 20;
    AsciiSPrint (SerialLine, sizeof (SerialLine),
                 "\rFastboot %a %a: %Lu/%Lu MiB (%u%%)    ",
                 mFlashTarget, Verify ? "verify" : "write",
                 DoneMib, TotalMib, Percent);
    SerialPortWrite ((UINT8 *)SerialLine, AsciiStrLen (SerialLine));
    AsciiSPrint (Value, sizeof (Value), "%a %Lu/%Lu MiB",
                 Verify ? "verifying" : "writing",
                 DoneMib, TotalMib);
    Status = Reply ("INFO", Value);
    mProgressNow = FALSE;
    mVerify = Verify;
    mLastProgress = GetPerformanceCounter ();
  }
  return Status;
}

STATIC BOOLEAN
HexNumber (CONST CHAR8 *Text, UINTN Digits, UINT64 *Value)
{
  *Value = 0;
  if (Digits == 0 || Digits > 16)
    return FALSE;
  for (UINTN I = 0; I < Digits; I++)
  {
    CHAR8 C = Text[I];
    UINTN N;
    if (C >= '0' && C <= '9')
      N = C - '0';
    else if (C >= 'a' && C <= 'f')
      N = C - 'a' + 10;
    else if (C >= 'A' && C <= 'F')
      N = C - 'A' + 10;
    else
      return FALSE;
    *Value = (*Value << 4) | N;
  }
  return TRUE;
}

STATIC BOOLEAN
HexRange (CONST CHAR8 *Text, UINTN Digits, UINT64 *Value)
{
  if (Digits >= 2 && Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X'))
  {
    Text += 2;
    Digits -= 2;
  }
  return HexNumber (Text, Digits, Value);
}

STATIC EFI_STATUS
Fetch (CHAR8 *Command, VOID *Buffer)
{
  CHAR8 Name[48], Value[64], *OffsetText, *LengthText;
  UINT64 Capacity, Offset = 0, Length;
  EFI_STATUS Status;

  OffsetText = AsciiStrStr (Command, ":");
  if (OffsetText != NULL)
    *OffsetText++ = 0;
  AsciiSPrint (Name, sizeof (Name), "partition-size:%a", Command);
  Status = FastbootGetVar (Name, Value, sizeof (Value));
  if (EFI_ERROR (Status))
    return Reply ("FAIL", "unknown partition");
  Capacity = AsciiStrHexToUint64 (Value);
  Length = Capacity;
  if (OffsetText != NULL)
  {
    LengthText = AsciiStrStr (OffsetText, ":");
    if (LengthText == NULL
        || !HexRange (OffsetText, LengthText - OffsetText, &Offset)
        || !HexRange (LengthText + 1, AsciiStrLen (LengthText + 1),
                      &Length))
      return Reply ("FAIL", "invalid read range");
  }
  if (Length == 0 || Length > FASTBOOT_DOWNLOAD_MAX || Offset > Capacity
      || Length > Capacity - Offset)
    return Reply ("FAIL", "read range exceeds partition or buffer");
  Status = FastbootRead (Command, Offset, (UINTN)Length, Buffer);
  if (EFI_ERROR (Status))
    return Reply ("FAIL", "partition read failed");
  AsciiSPrint (Value, sizeof (Value), "%08x", (UINT32)Length);
  Status = Reply ("DATA", Value);
  for (UINTN Sent = 0; !EFI_ERROR (Status) && Sent < Length;)
  {
    UINTN Bytes = MIN (PACKET_BYTES, (UINTN)Length - Sent);
    Status = FastbootUsbSend ((UINT8 *)Buffer + Sent, Bytes);
    Sent += Bytes;
  }
  return EFI_ERROR (Status) ? Status : Reply ("OKAY", "");
}

EFI_STATUS EFIAPI
FastbootMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  VOID *Download = NULL;
  UINT8 *Packet = NULL;
  UINTN Expected = 0, Done = 0, Size;
  UINT64 LastData = 0;
  BOOLEAN Complete = FALSE, Exit = FALSE;
  CHAR8 Command[65], Value[64];

  Status = FastbootPlatformInit ();
  if (EFI_ERROR (Status))
    return Status;
  Download = AllocatePool (FASTBOOT_DOWNLOAD_MAX);
  Packet = AllocatePool (PACKET_BYTES);
  if (Download == NULL || Packet == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  Status = FastbootUsbStart ();
  if (EFI_ERROR (Status))
    goto Out;
  Print (L"USB Fastboot. ESC or fastboot continue exits.\r\n");
  while (!Exit)
  {
    if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))
        && Key.ScanCode == SCAN_ESC)
    {
      Status = EFI_SUCCESS;
      break;
    }
    Size = Expected ? MIN (Expected - Done, PACKET_BYTES) : 512;
    Status = FastbootUsbReceive (Packet, &Size);
    if (Status == EFI_MEDIA_CHANGED)
    {
      Expected = Done = 0;
      Complete = FALSE;
      continue;
    }
    if (Status == EFI_NOT_READY)
    {
      if (Expected && GetTimeInNanoSecond (
              GetPerformanceCounter () - LastData) >= DOWNLOAD_TIMEOUT_NS)
      {
        Expected = Done = 0;
        Complete = FALSE;
        Print (L"Download timed out; USB session restarted.\r\n");
        FastbootUsbStop ();
        Status = FastbootUsbStart ();
        if (EFI_ERROR (Status))
          break;
      }
      MicroSecondDelay (20);
      continue;
    }
    if (EFI_ERROR (Status))
      break;
    if (Expected)
    {
      if (Size == 0)
        continue;
      if (Size > Expected - Done)
      {
        Expected = 0;
        Complete = FALSE;
        Status = Reply ("FAIL", "invalid data size");
      }
      else
      {
        CopyMem ((UINT8 *)Download + Done, Packet, Size);
        Done += Size;
        LastData = GetPerformanceCounter ();
        if (Done == Expected)
        {
          Expected = 0;
          Complete = TRUE;
          Status = Reply ("OKAY", "");
        }
      }
      if (EFI_ERROR (Status))
        break;
      continue;
    }
    if (!Size || Size > 64 || ScanMem8 (Packet, Size, 0) != NULL)
    {
      Status = Reply ("FAIL", "invalid command");
      if (EFI_ERROR (Status))
        break;
      continue;
    }
    CopyMem (Command, Packet, Size);
    Command[Size] = 0;
    if (!AsciiStrnCmp (Command, "download:", 9))
    {
      UINT64 Bytes;
      Complete = FALSE;
      Done = 0;
      if (Size != 17 || !HexNumber (Command + 9, 8, &Bytes) || Bytes == 0
          || Bytes > FASTBOOT_DOWNLOAD_MAX)
        Status = Reply ("FAIL", "invalid download size");
      else
      {
        Expected = (UINTN)Bytes;
        LastData = GetPerformanceCounter ();
        AsciiSPrint (Value, sizeof (Value), "%08x", (UINT32)Bytes);
        Status = Reply ("DATA", Value);
      }
    }
    else if (!AsciiStrnCmp (Command, "getvar:", 7))
    {
      CONST CHAR8 *Name = Command + 7;
      if (!AsciiStrCmp (Name, "version"))
        Status = Reply ("OKAY", "0.4");
      else if (!AsciiStrCmp (Name, "is-userspace"))
        Status = Reply ("OKAY", "no");
      else if (!AsciiStrCmp (Name, "max-download-size")
               || !AsciiStrCmp (Name, "max-fetch-size"))
      {
        AsciiSPrint (Value, sizeof (Value), "0x%08x",
                     FASTBOOT_DOWNLOAD_MAX);
        Status = Reply ("OKAY", Value);
      }
      else if (!AsciiStrCmp (Name, "all"))
      {
        Status = FastbootGetVar ("product", Value, sizeof (Value));
        if (!EFI_ERROR (Status))
        {
          CHAR8 Info[64];
          AsciiSPrint (Info, sizeof (Info), "product: %a", Value);
          Status = Reply ("INFO", Info);
        }
        if (!EFI_ERROR (Status))
          Status = Reply ("OKAY", "");
      }
      else
      {
        Status = FastbootGetVar (Name, Value, sizeof (Value));
        Status = Reply (EFI_ERROR (Status) ? "FAIL" : "OKAY",
                        EFI_ERROR (Status) ? "unknown variable" : Value);
      }
    }
    else if (!AsciiStrnCmp (Command, "flash:", 6))
    {
      if (!Complete)
      {
        Status = Reply ("FAIL", "no complete download");
        if (EFI_ERROR (Status))
          break;
        continue;
      }
      mProgressNow = TRUE;
      mFlashTarget = Command + 6;
      Status = FastbootFlash (Command + 6, Download, Done, FlashProgress);
      Complete = FALSE;
      Done = 0;
      CHAR8 Result[96];
      AsciiSPrint (Result, sizeof (Result), "\r\nFastboot %a: %r\r\n",
                   mFlashTarget, Status);
      SerialPortWrite ((UINT8 *)Result, AsciiStrLen (Result));
      if (Status == EFI_MEDIA_CHANGED || Status == EFI_TIMEOUT
          || Status == EFI_NOT_READY)
      {
        Print (L"Flash interrupted; USB session restarted.\r\n");
        FastbootUsbStop ();
        Status = FastbootUsbStart ();
        if (EFI_ERROR (Status))
          break;
        continue;
      }
      if (Status == EFI_ABORTED)
        Print (L"Flash canceled; restart the complete flash command.\r\n");
      AsciiSPrint (Value, sizeof (Value), "%r", Status);
      Status = Reply (EFI_ERROR (Status) ? "FAIL" : "OKAY",
                      EFI_ERROR (Status) ? Value : "");
      if (EFI_ERROR (Status))
      {
        FastbootUsbStop ();
        Status = FastbootUsbStart ();
      }
    }
    else if (!AsciiStrnCmp (Command, "fetch:", 6))
    {
      Complete = FALSE;
      Status = Fetch (Command + 6, Download);
    }
    else if (!AsciiStrCmp (Command, "continue"))
    {
      Status = Reply ("OKAY", "");
      Exit = TRUE;
    }
    else if (!AsciiStrCmp (Command, "boot")
             || !AsciiStrCmp (Command, "oem boot-efi"))
    {
      EFI_HANDLE Loaded = NULL;
      Status = Complete ? gBS->LoadImage (FALSE, ImageHandle, NULL,
                                          Download, Done, &Loaded)
                        : EFI_NOT_READY;
      if (EFI_ERROR (Status))
        Status = Reply ("FAIL", "no loadable EFI image");
      else
      {
        Status = Reply ("OKAY", "");
        if (!EFI_ERROR (Status))
        {
          FastbootUsbStop ();
          Status = gBS->StartImage (Loaded, NULL, NULL);
          Exit = TRUE;
        }
        gBS->UnloadImage (Loaded);
      }
    }
    else
      Status = Reply ("FAIL", "unsupported command");
    if (EFI_ERROR (Status))
      break;
  }
Out:
  FastbootUsbStop ();
  FastbootPlatformStop ();
  if (Download != NULL)
    FreePool (Download);
  if (Packet != NULL)
    FreePool (Packet);
  if (EFI_ERROR (Status))
    Print (L"Fastboot stopped: %r\r\n", Status);
  return Status;
}
