// SPDX-License-Identifier: GPL-2.0-only
#include "../platforms/RiscVPlatformPkg/Drivers/Dc8200Dxe/Gop.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

EFI_GUID gEfiDevicePathProtocolGuid, gEfiGraphicsOutputProtocolGuid;
EFI_GUID gEfiEdidDiscoveredProtocolGuid, gEfiEdidActiveProtocolGuid;
static BOOLEAN Connected, BadEdid;
static unsigned Installs, Reinstalls, Disconnects, Stops, Modes, Flushes;
static UINT8 Pixels[1024];
static UINT16 HFront;
CONST DISPLAY_TIMING DisplayTimings[DISPLAY_STANDARD_MODES] = {
  {.Width = 8, .Height = 8}, {.Width = 8, .Height = 4}
};
BOOLEAN DisplayConnected (DISPLAY_HW *Hw) { return Connected; }
EFI_STATUS DisplayReadEdid (DISPLAY_HW *Hw)
{
  Hw->EdidSize = 128;
  return BadEdid ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
VOID DisplayParseEdid (DISPLAY_HW *Hw)
{
  Hw->ModeCount = 2;
  Hw->Modes[0] = DisplayTimings[0];
  Hw->Modes[1] = DisplayTimings[1];
  Hw->Modes[0].HFront = HFront;
}
EFI_STATUS DisplaySetTiming (DISPLAY_HW *Hw, CONST DISPLAY_TIMING *Mode,
                             EFI_PHYSICAL_ADDRESS Buffer)
{
  assert (Connected && Buffer == (UINTN)Pixels);
  Modes++;
  return EFI_SUCCESS;
}
VOID DisplayDisable (DISPLAY_HW *Hw) { Stops++; }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset (P, 0, N); }
VOID *EFIAPI WriteBackDataCacheRange (VOID *P, UINTN N)
{ Flushes++; return P; }
VOID *EFIAPI WriteBackInvalidateDataCacheRange (VOID *P, UINTN N)
{ Flushes++; return P; }
UINTN EFIAPI AsciiVSPrint (CHAR8 *P, UINTN N, CONST CHAR8 *F, VA_LIST A)
{ return 0; }
UINTN EFIAPI SerialPortWrite (UINT8 *P, UINTN N) { return N; }
RETURN_STATUS EFIAPI FrameBufferBltConfigure (
    VOID *Buffer, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info,
    FRAME_BUFFER_CONFIGURE *Configure, UINTN *Size)
{ return RETURN_SUCCESS; }
static EFI_TPL EFIAPI Raise (EFI_TPL Level) { return TPL_APPLICATION; }
static VOID EFIAPI Restore (EFI_TPL Level) {}
static EFI_STATUS EFIAPI Disconnect (
    EFI_HANDLE Controller, EFI_HANDLE Driver, EFI_HANDLE Child)
{
  assert (Controller == (EFI_HANDLE)1);
  Disconnects++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Install (EFI_HANDLE *Handle, ...)
{
  assert (!*Handle);
  *Handle = (EFI_HANDLE)1;
  Installs++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Reinstall (
    EFI_HANDLE Handle, EFI_GUID *Guid, VOID *Old, VOID *New)
{
  assert (Handle == (EFI_HANDLE)1 && Old == New);
  Reinstalls++;
  return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES Services = {
  .RaiseTPL = Raise, .RestoreTPL = Restore,
  .DisconnectController = Disconnect,
  .InstallMultipleProtocolInterfaces = Install,
  .ReinstallProtocolInterface = Reinstall
};
EFI_BOOT_SERVICES *gBS = &Services;

static void Poll (void)
{
  for (unsigned I = 0; I < 5; I++)
    Refresh (NULL, &mDisplay);
}
int main (void)
{
  mDisplay.Buffer = (UINTN)Pixels;
  mDisplay.BufferSize = sizeof (Pixels);
  mDisplay.Mode.Mode = MAX_UINT32;
  assert (Probe (&mDisplay) == EFI_NOT_FOUND && !Installs);
  Connected = TRUE;
  BadEdid = TRUE;
  Poll ();
  assert (!Installs && !Modes);
  BadEdid = FALSE;
  Poll ();
  assert (Installs == 1 && mDisplay.Online && Modes == 1);
  memset (Pixels, 0x5a, sizeof (Pixels));
  Connected = FALSE;
  Poll ();
  assert (!mDisplay.Online && Stops == 1);
  Poll ();
  assert (Stops == 1 && !Reinstalls);
  Connected = TRUE;
  Poll ();
  assert (mDisplay.Online && !Disconnects && !Reinstalls);
  assert (Pixels[0] == 0x5a);
  assert (Installs == 1 && Modes == 2);
  assert (mDisplay.Info.PixelsPerScanLine == 32);
  assert (mDisplay.Mode.FrameBufferSize == sizeof (Pixels));
  // A different sink can use the same dimensions with different porches.
  Connected = FALSE;
  Poll ();
  HFront = 8;
  Connected = TRUE;
  Poll ();
  assert (Disconnects == 1 && Reinstalls == 1 && Modes == 3);
  assert (mDisplay.Hw.Modes[0].HFront == 8 && Pixels[0] == 0);
  Exit (NULL, &mDisplay);
  unsigned Before = Flushes;
  Connected = FALSE;
  Poll ();
  assert (Stops == 2 && Flushes == Before);
  puts ("HDMI: late attach, EDID retry, reconnect and OS handoff passed");
}
