// SPDX-License-Identifier: BSD-2-Clause-Patent
// The runtime reset path must reach SBI before touching S-mode MMIO.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseRiscVSbiLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static jmp_buf Stop;
static BOOLEAN Supported, SbiCalled, InterruptsOff;
static unsigned Writes, WdtWrites;
static UINTN ExpectedType = SBI_SRST_RESET_TYPE_COLD_REBOOT;
static BOOLEAN RouteEnabled;
VOID EFIAPI ResetCold (VOID);
VOID EFIAPI ResetShutdown (VOID);
EFI_STATUS EFIAPI SbiSystemReset (UINTN Type, UINTN Reason)
{
  assert (Type == ExpectedType);
  assert (Reason == SBI_SRST_RESET_REASON_NONE && !Writes);
  SbiCalled = TRUE;
  if (Supported) longjmp (Stop, 1);
  return EFI_UNSUPPORTED;
}
UINT32 EFIAPI MmioRead32 (UINTN Address) { assert (SbiCalled); return 0; }
UINT32 EFIAPI MmioWrite32 (UINTN Address, UINT32 Value)
{
  assert (SbiCalled && !Supported);
  Writes++;
  if (Address == 0xfffff44140ULL) {
    assert (Value & BIT8);
    RouteEnabled = TRUE;
  }
  if (Address >= 0xffefc30000ULL && Address < 0xffefc31000ULL) {
    static const UINTN Offsets[] = {4, 0, 12};
    static const UINT32 Values[] = {0, 1, 0x76};
    assert (InterruptsOff && RouteEnabled && WdtWrites < 3);
    assert (Address == 0xffefc30000ULL + Offsets[WdtWrites]);
    assert (Value == Values[WdtWrites++]);
  }
  return Value;
}
UINT32 EFIAPI MmioOr32 (UINTN Address, UINT32 Value)
{ return MmioWrite32 (Address, Value); }
UINT32 EFIAPI MmioAnd32 (UINTN Address, UINT32 Value)
{ return MmioWrite32 (Address, Value); }
UINTN EFIAPI MicroSecondDelay (UINTN Us) { return Us; }
VOID EFIAPI DisableInterrupts (VOID) { InterruptsOff = TRUE; }
VOID EFIAPI CpuDeadLoop (VOID) { longjmp (Stop, 2); }
int main (void)
{
  Supported = TRUE;
  if (!setjmp (Stop)) { ResetCold (); assert (0); }
  assert (SbiCalled && !Writes);
  Supported = FALSE;
  SbiCalled = FALSE;
  if (!setjmp (Stop)) { ResetCold (); assert (0); }
  assert (SbiCalled && Writes && WdtWrites == 3);
  Supported = TRUE;
  ExpectedType = SBI_SRST_RESET_TYPE_SHUTDOWN;
  Writes = WdtWrites = 0;
  SbiCalled = FALSE;
  if (!setjmp (Stop)) { ResetShutdown (); assert (0); }
  assert (SbiCalled && !Writes);
  Supported = FALSE;
  SbiCalled = FALSE;
  ResetShutdown ();
  assert (SbiCalled && Writes && !WdtWrites);
  puts ("EFI reset: SRST first for reboot/shutdown, routed WDT fallback only for reboot");
}
