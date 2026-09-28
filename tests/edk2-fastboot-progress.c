// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../platforms/RiscVPlatformPkg/Application/Fastboot/Fastboot.c"

static EFI_STATUS UsbStatus = EFI_SUCCESS, SendStatus = EFI_SUCCESS;
static UINT64 Clock;
static BOOLEAN Escape;
static UINTN Sends, ConsoleUpdates;
static CHAR8 Response[65], LastSerial[128];
EFI_STATUS FastbootUsbPoll (VOID) { return UsbStatus; }
EFI_STATUS FastbootUsbSend (CONST VOID *Data, UINTN Size)
{
  assert (Size <= 64);
  memcpy (Response, Data, Size);
  Response[Size] = 0;
  Sends++;
  return SendStatus;
}
UINTN EFIAPI SerialPortWrite (UINT8 *Buffer, UINTN Size)
{
  CHAR8 Text[128];
  assert (Size < sizeof (Text));
  memcpy (Text, Buffer, Size);
  Text[Size] = 0;
  assert (!strncmp (Text, "\rFastboot emmc0 ", 16));
  assert (strstr (Text, " MiB (") != NULL);
  strcpy (LastSerial, Text);
  assert (strstr (Text, "write:") || strstr (Text, "verify:"));
  ConsoleUpdates++;
  return Size;
}
UINT64 EFIAPI GetPerformanceCounter (VOID) { return Clock; }
UINT64 EFIAPI GetTimeInNanoSecond (UINT64 Ticks) { return Ticks; }
UINTN EFIAPI AsciiStrLen (CONST CHAR8 *S) { return strlen (S); }
UINTN EFIAPI AsciiSPrint (CHAR8 *B, UINTN N, CONST CHAR8 *Format, ...)
{
  CHAR8 HostFormat[128];
  va_list Ap;
  strcpy (HostFormat, Format);
  for (UINTN I = 0; HostFormat[I]; I++)
    if (HostFormat[I] == '%' && HostFormat[I + 1] == 'a')
      HostFormat[I + 1] = 's';
  va_start (Ap, Format);
  int Size = vsnprintf (B, N, HostFormat, Ap);
  va_end (Ap);
  return Size;
}
static EFI_STATUS EFIAPI
ReadKey (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key)
{
  (void)This;
  Key->ScanCode = SCAN_ESC;
  return Escape ? EFI_SUCCESS : EFI_NOT_READY;
}
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input = {.ReadKeyStroke = ReadKey};
static EFI_SYSTEM_TABLE System = {.ConIn = &Input};
EFI_SYSTEM_TABLE *gST = &System;
int main (void)
{
  mFlashTarget = "emmc0";
  mProgressNow = TRUE;
  assert (FlashProgress (0, SIZE_64MB, FALSE) == EFI_SUCCESS);
  assert (!strcmp (Response, "INFOwriting 0/64 MiB") && Sends == 1);
  assert (FlashProgress (1, SIZE_64MB, FALSE) == EFI_SUCCESS
          && Sends == 1);
  Clock = 1000000000ULL;
  assert (FlashProgress (SIZE_1MB, SIZE_64MB, FALSE) == EFI_SUCCESS);
  assert (!strcmp (Response, "INFOwriting 1/64 MiB") && Sends == 2);
  assert (FlashProgress (0, SIZE_64MB, TRUE) == EFI_SUCCESS && Sends == 3);
  assert (!strcmp (Response, "INFOverifying 0/64 MiB"));
  Escape = TRUE;
  assert (FlashProgress (0, SIZE_64MB, FALSE) == EFI_ABORTED
          && Sends == 3);
  Escape = FALSE; UsbStatus = EFI_MEDIA_CHANGED;
  assert (FlashProgress (0, SIZE_64MB, FALSE) == EFI_MEDIA_CHANGED);
  UsbStatus = EFI_SUCCESS; SendStatus = EFI_TIMEOUT; mProgressNow = TRUE;
  assert (FlashProgress (0, SIZE_64MB, FALSE) == EFI_TIMEOUT);
  SendStatus = EFI_SUCCESS;
  UINT64 Small = 2 * SIZE_1MB + 512;
  assert (FlashProgress (Small, Small, TRUE) == EFI_SUCCESS);
  assert (!strcmp (Response, "INFOverifying 3/3 MiB"));
  assert (strstr (LastSerial, "verify: 3/3 MiB (100%)") != NULL);
  assert (FlashProgress (0, 0, TRUE) == EFI_SUCCESS);
  assert (strstr (LastSerial, "verify: 0/0 MiB (100%)") != NULL);
  assert (ConsoleUpdates == Sends);
  puts ("Fastboot serial/USB progress and cancellation passed");
  return 0;
}
