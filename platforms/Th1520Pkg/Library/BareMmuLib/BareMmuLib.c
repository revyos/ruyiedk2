// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

// TH1520 peripherals live above the Sv39 identity-mapping range. Keep the
// firmware in physical addressing mode, using the PMAs configured by SPL.
EFI_STATUS EFIAPI
RiscVConfigureMmu (VOID)
{
  ASSERT (RiscVGetSupervisorAddressTranslationRegister () == 0);
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI
RiscVSetMemoryAttributes (EFI_PHYSICAL_ADDRESS Base, UINT64 Length, UINT64 Attributes)
{
  if (!Length || (Base & EFI_PAGE_MASK) || (Length & EFI_PAGE_MASK))
  {
    return EFI_INVALID_PARAMETER;
  }
  Attributes &= ~EFI_MEMORY_RUNTIME;
  if (Attributes & ~(EFI_MEMORY_WB | EFI_MEMORY_UC))
  {
    return EFI_UNSUPPORTED;
  }
  if (Base < 0x400000000ULL && Length <= 0x400000000ULL - Base && Attributes == EFI_MEMORY_WB)
  {
    return EFI_SUCCESS;
  }
  if (Base >= 0xff00000000ULL && Attributes == EFI_MEMORY_UC)
  {
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}
VOID EFIAPI
RiscVLocalTlbFlushAll (VOID)
{
  __asm__ volatile ("sfence.vma" ::: "memory");
}
VOID EFIAPI
RiscVLocalTlbFlush (UINTN Address)
{
  __asm__ volatile ("sfence.vma %0" ::"r"(Address) : "memory");
}
