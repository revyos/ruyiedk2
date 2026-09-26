// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <A210.h>
#include <Library/A210BootMediaLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/EqosPlatformLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/RngLib.h>
#include <Library/UefiLib.h>

#define MAC_STORE_MAGIC SIGNATURE_32 ('A', 'M', 'A', 'C')
#define MAC_STORE_BYTES 512

typedef struct
{
  UINT32 Magic, Version, Crc;
  UINT8 Address[6];
  UINT8 Reserved[494];
} MAC_RECORD;
STATIC_ASSERT (sizeof (MAC_RECORD) == MAC_STORE_BYTES,
               "MAC record occupies one eMMC sector");

STATIC BOOLEAN
Valid (CONST UINT8 *Mac)
{
  CONST UINT8 Zero[6] = { 0 };
  return !(Mac[0] & 1) && CompareMem (Mac, Zero, 6) != 0;
}
STATIC EFI_STATUS
Fallback (UINT8 Base[6], BOOLEAN *Persistent)
{
  EFI_BLOCK_IO_PROTOCOL *Boot[2], *Io;
  MAC_RECORD *Record, *Verify;
  EFI_STATUS Status;
  UINT64 Random;
  UINT32 Crc;
  EFI_LBA Lba = A210_MAC_STORE_OFFSET / MAC_STORE_BYTES;

  *Persistent = FALSE;
  Status = A210LocateBootAreas (Boot);
  Io = EFI_ERROR (Status) ? NULL : Boot[0];
  if (!Io || Io->Media->LastBlock < Lba)
    return EFI_NOT_READY;
  Record = AllocatePages (1);
  Verify = AllocatePages (1);
  if (!Record || !Verify)
  {
    if (Record)
      FreePages (Record, 1);
    if (Verify)
      FreePages (Verify, 1);
    return EFI_OUT_OF_RESOURCES;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, Lba, MAC_STORE_BYTES,
                           Record);
  if (EFI_ERROR (Status))
    goto Done;
  Crc = Record->Crc;
  Record->Crc = 0;
  if (Record->Magic == MAC_STORE_MAGIC && Record->Version == 1
      && Crc == CalculateCrc32 (Record, sizeof (*Record))
      && Valid (Record->Address) && (Record->Address[0] & 3) == 2
      && !(Record->Address[5] & 1))
  {
    CopyMem (Base, Record->Address, 6);
    *Persistent = TRUE;
    goto Done;
  }
  if (!GetRandomNumber64 (&Random))
  {
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }
  CopyMem (Base, &Random, 6);
  Base[0] = (Base[0] & ~3U) | 2;
  Base[5] &= ~1U;
  ZeroMem (Record, sizeof (*Record));
  Record->Magic = MAC_STORE_MAGIC;
  Record->Version = 1;
  CopyMem (Record->Address, Base, 6);
  Record->Crc = CalculateCrc32 (Record, sizeof (*Record));
  Status = Io->WriteBlocks (Io, Io->Media->MediaId, Lba, MAC_STORE_BYTES,
                            Record);
  if (!EFI_ERROR (Status))
    Status = Io->FlushBlocks (Io);
  if (!EFI_ERROR (Status))
    Status = Io->ReadBlocks (Io, Io->Media->MediaId, Lba, MAC_STORE_BYTES,
                             Verify);
  *Persistent = !EFI_ERROR (Status)
                && CompareMem (Record, Verify, sizeof (*Record)) == 0;
  // A failed save still permits networking with a temporary address.
  Status = EFI_SUCCESS;
Done:
  FreePages (Verify, 1);
  FreePages (Record, 1);
  return Status;
}
EFI_STATUS
GmacSelectAddress (FDT_CLIENT_PROTOCOL *Fdt, GMAC_HARDWARE *Hw,
                   UINT8 *Address)
{
  STATIC UINT8 Base[6];
  STATIC BOOLEAN Ready, Persistent;
  CONST VOID *Data;
  UINT32 Size;
  EFI_STATUS Status;
  CONST CHAR8 *Names[] = { "mac-address", "local-mac-address" };
  BOOLEAN Supplied = FALSE;

  for (UINTN I = 0; I < ARRAY_SIZE (Names); I++)
  {
    Status = Fdt->GetNodeProperty (Fdt, Hw->Node, Names[I], &Data, &Size);
    if (!EFI_ERROR (Status) && Size == 6 && Valid (Data))
    {
      CopyMem (Address, Data, 6);
      Supplied = TRUE;
      break;
    }
  }
  if (!Supplied)
  {
    if (!Ready)
    {
      Status = Fallback (Base, &Persistent);
      if (EFI_ERROR (Status))
        return Status;
      Ready = TRUE;
    }
    CopyMem (Address, Base, 6);
    Address[5] += Hw->Index;
    Print (L"GMAC%u: no valid DT MAC; using %s random address\r\n",
           Hw->Index, Persistent ? L"saved" : L"temporary");
  }
  // Replace an invalid higher-priority property as well.
  Status = Fdt->SetNodeProperty (Fdt, Hw->Node, "mac-address", Address, 6);
  if (EFI_ERROR (Status))
    return Status;
  return Fdt->SetNodeProperty (Fdt, Hw->Node, "local-mac-address", Address,
                               6);
}
