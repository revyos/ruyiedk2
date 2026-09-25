// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Base.h>
#include <Library/DebugLib.h>
#include <Library/CacheMaintenanceLib.h>

STATIC VOID *
CacheRange (VOID *Address, UINTN Length, BOOLEAN CleanOnly)
{
  UINTN Start, Last;
  if (Length == 0)
  {
    return Address;
  }
  ASSERT (Length - 1 <= MAX_ADDRESS - (UINTN)Address);
  Start = (UINTN)Address & ~63UL;
  Last = ((UINTN)Address + Length - 1) & ~63UL;
  for (;;)
  {
    // VA operations also remain valid if an OS enables translation at runtime.
    if (CleanOnly)
    {
      __asm__ volatile (".insn r 0x0b, 0, 1, x0, %0, x5" ::"r"(Start) : "memory");
    }
    else
    {
      __asm__ volatile (".insn r 0x0b, 0, 1, x0, %0, x7" ::"r"(Start) : "memory");
    }
    if (Start == Last)
    {
      break;
    }
    Start += 64;
  }
  __asm__ volatile (".long 0x0190000b" ::: "memory");
  return Address;
}
VOID EFIAPI
InvalidateInstructionCache (VOID)
{
  __asm__ volatile ("fence.i" ::: "memory");
}
VOID *EFIAPI
InvalidateInstructionCacheRange (VOID *Address, UINTN Length)
{
  CacheRange (Address, Length, TRUE);
  InvalidateInstructionCache ();
  return Address;
}
VOID EFIAPI
WriteBackInvalidateDataCache (VOID)
{
  __asm__ volatile (".long 0x0030000b; .long 0x0190000b" ::: "memory");
}
VOID EFIAPI
WriteBackDataCache (VOID)
{
  __asm__ volatile (".long 0x0010000b; .long 0x0190000b" ::: "memory");
}
VOID EFIAPI
InvalidateDataCache (VOID)
{
  WriteBackInvalidateDataCache ();
}
VOID *EFIAPI
WriteBackInvalidateDataCacheRange (VOID *Address, UINTN Length)
{
  return CacheRange (Address, Length, FALSE);
}
VOID *EFIAPI
WriteBackDataCacheRange (VOID *Address, UINTN Length)
{
  return CacheRange (Address, Length, TRUE);
}
VOID *EFIAPI
InvalidateDataCacheRange (VOID *Address, UINTN Length)
{
  return CacheRange (Address, Length, FALSE);
}
