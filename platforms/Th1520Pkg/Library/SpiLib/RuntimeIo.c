// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "RuntimeIo.h"
#include <Library/Th1520SpiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>

// C910 MAEE: normal memory is C/B/SH; device memory is SO/SH.
#define PTE_NORMAL (BIT62 | BIT61 | BIT60)
#define PTE_DEVICE (BIT63 | BIT60)
#define PTE_CODE (PTE_NORMAL | 0xcb) // V, R, X, A, D
#define PTE_MMIO (PTE_DEVICE | 0xc7) // V, R, W, A, D
STATIC UINT64 *Tables;
STATIC UINTN Used, RuntimeSatp;
UINT32 EFIAPI Th1520SpiMmio (
    UINTN Address, UINT32 Value, UINTN Satp, BOOLEAN Write);

EFI_STATUS
Th1520SpiRuntimePrepare (VOID *Pages)
{
  if (!Pages || ((UINTN)Pages & EFI_PAGE_MASK))
    return EFI_INVALID_PARAMETER;
  Tables = Pages;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Map (UINTN Virtual, UINTN Physical, UINT64 Flags)
{
  UINT64 *Table = Tables;
  // Runtime mappings on this Sv39 platform must be canonical.
  if ((INT64)(Virtual << 25) >> 25 != (INT64)Virtual)
    return EFI_UNSUPPORTED;
  for (INTN Level = 2; Level >= 0; Level--)
  {
    UINTN Index = (Virtual >> (12 + 9 * Level)) & 511;
    if (!Level)
    {
      Table[Index] = ((Physical >> 12) << 10) | Flags;
      return EFI_SUCCESS;
    }
    if (!Table[Index])
    {
      if (Used == TH1520_SPI_RUNTIME_PAGES)
        return EFI_OUT_OF_RESOURCES;
      Table[Index] = ((UINTN)(Tables + 512 * Used++) >> 12) << 10 | 1;
    }
    Table = (UINT64 *)(UINTN)((Table[Index] >> 10) << 12);
  }
  return EFI_DEVICE_ERROR;
}

EFI_STATUS
Th1520SpiMapRuntimeIo (EFI_CONVERT_POINTER Convert, UINTN Spi, UINTN Ap)
{
  VOID *Code = (VOID *)(UINTN)Th1520SpiMmio;
  EFI_STATUS Status;
  if (!Tables)
    return EFI_NOT_READY;
  Status = Convert (0, &Code);
  if (EFI_ERROR (Status))
    return Status;
  ZeroMem (Tables, TH1520_SPI_RUNTIME_PAGES * EFI_PAGE_SIZE);
  Used = 1;
  Status = Map ((UINTN)Code, (UINTN)Th1520SpiMmio, PTE_CODE);
  if (!EFI_ERROR (Status))
    Status = Map (Spi, 0xffea000000ULL, PTE_MMIO);
  if (!EFI_ERROR (Status))
    Status = Map (Ap, 0xffef010000ULL, PTE_MMIO);
  if (EFI_ERROR (Status))
    return Status;
  WriteBackDataCacheRange (Tables,
                           TH1520_SPI_RUNTIME_PAGES * EFI_PAGE_SIZE);
  RuntimeSatp = BIT63 | ((UINTN)Tables >> 12); // Sv39, ASID 0.
  return EFI_SUCCESS;
}

UINT32
Th1520SpiMmioRead32 (UINTN Address)
{
  return RuntimeSatp ? Th1520SpiMmio (Address, 0, RuntimeSatp, FALSE) :
                       MmioRead32 (Address);
}

UINT32
Th1520SpiMmioWrite32 (UINTN Address, UINT32 Value)
{
  return RuntimeSatp ? Th1520SpiMmio (Address, Value, RuntimeSatp, TRUE) :
                       MmioWrite32 (Address, Value);
}
