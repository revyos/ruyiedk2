// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/FastbootStorageLib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UINT8 Disk[256 * 1024];
static UINT64 Base;
static UINTN Writes, Reads, Flushes;
static BOOLEAN BadRead, BadWrite, BadFlush;
UINT16 EFIAPI ReadUnaligned16 (CONST VOID *P)
{ UINT16 V; memcpy (&V, P, 2); return V; }
UINT32 EFIAPI ReadUnaligned32 (CONST VOID *P)
{ UINT32 V; memcpy (&V, P, 4); return V; }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memcpy (D, S, N); }
VOID *EFIAPI SetMem (VOID *D, UINTN N, UINT8 V)
{ return memset (D, V, N); }
VOID *EFIAPI SetMem32 (VOID *D, UINTN N, UINT32 V)
{
  for (UINTN I = 0; I < N / 4; I++) ((UINT32 *)D)[I] = V;
  return D;
}
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI AllocatePages (UINTN N)
{ return aligned_alloc (EFI_PAGE_SIZE, N * EFI_PAGE_SIZE); }
VOID EFIAPI FreePages (VOID *P, UINTN N) { (void)N; free (P); }

static EFI_STATUS EFIAPI
Read (EFI_BLOCK_IO_PROTOCOL *Io, UINT32 Id, EFI_LBA Lba,
      UINTN Size, VOID *Data)
{
  UINT64 Offset = Lba * Io->Media->BlockSize;
  (void)Id;
  assert (Flushes && Offset >= Base);
  assert (Size % Io->Media->BlockSize == 0);
  assert (Offset - Base + Size <= sizeof (Disk));
  memcpy (Data, Disk + Offset - Base, Size);
  if (BadRead)
    ((UINT8 *)Data)[0] ^= 1;
  Reads++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI
Write (EFI_BLOCK_IO_PROTOCOL *Io, UINT32 Id, EFI_LBA Lba,
       UINTN Size, VOID *Data)
{
  UINT64 Offset = Lba * Io->Media->BlockSize;
  (void)Id;
  if (BadWrite)
    return EFI_DEVICE_ERROR;
  assert (Offset >= Base);
  assert (Size % Io->Media->BlockSize == 0);
  assert (Offset - Base + Size <= sizeof (Disk));
  assert ((UINTN)Data % EFI_PAGE_SIZE == 0);
  memcpy (Disk + Offset - Base, Data, Size);
  Writes++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *Io)
{
  (void)Io;
  Flushes++;
  return BadFlush ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_BLOCK_IO_MEDIA Media = {
  .MediaPresent = TRUE, .BlockSize = 512, .IoAlign = 4096,
  .LastBlock = sizeof (Disk) / 512 - 1
};
static EFI_BLOCK_IO_PROTOCOL Io = {
  .Media = &Media, .ReadBlocks = Read, .WriteBlocks = Write,
  .FlushBlocks = Flush
};
static void Put16 (UINT8 *P, UINT16 V) { memcpy (P, &V, 2); }
static void Put32 (UINT8 *P, UINT32 V) { memcpy (P, &V, 4); }
static void Header (UINT8 *P, UINT32 Blocks, UINT32 Chunks)
{
  memset (P, 0, 28);
  Put32 (P, 0xed26ff3a);
  Put16 (P + 4, 1);
  Put16 (P + 8, 28);
  Put16 (P + 10, 12);
  Put32 (P + 12, 512);
  Put32 (P + 16, Blocks);
  Put32 (P + 20, Chunks);
}
static void Chunk (UINT8 *P, UINT16 Type, UINT32 Blocks, UINT32 Payload)
{
  memset (P, 0, 12);
  Put16 (P, Type);
  Put32 (P + 4, Blocks);
  Put32 (P + 8, 12 + Payload);
}
static EFI_STATUS Flash (UINT8 *P, UINTN Size)
{
  Writes = Reads = Flushes = 0;
  return FastbootWriteImage (&Io, P, Size, TRUE, NULL);
}
static void Reject (UINT8 *P, UINTN Size)
{
  assert (EFI_ERROR (Flash (P, Size)));
  assert (Writes == 0 && Flushes == 0);
}
static BOOLEAN CancelVerify;
static EFI_STATUS CancelStatus;
static UINTN ProgressCalls;
static EFI_STATUS Cancel (UINT64 Done, UINT64 Total, BOOLEAN Verify)
{
  assert (Total == 3 * SIZE_64KB && Done <= Total);
  ProgressCalls++;
  if (Verify == CancelVerify && Done >= SIZE_64KB)
    return CancelStatus;
  return EFI_SUCCESS;
}
static UINTN Completions;
static EFI_STATUS RecordCompletion (UINT64 Done, UINT64 Total,
                                    BOOLEAN Verify)
{
  assert (Done <= Total);
  if (Done == Total)
  {
    assert (Flushes == 1);
    Completions |= Verify ? 2 : 1;
  }
  return EFI_SUCCESS;
}
int main (void)
{
  UINT8 Image[2048] = {0}, Bad[2048];
  UINTN Size;

  memset (Disk, 0xa5, sizeof (Disk));
  memset (Image, 0x12, 513);
  assert (Flash (Image, 513) == EFI_SUCCESS);
  assert (Writes == 1 && Reads == 1 && Flushes == 1);
  assert (!memcmp (Image, Disk, 513));
  for (UINTN I = 513; I < 1024; I++) assert (Disk[I] == 0xff);
  assert (Disk[1024] == 0xa5);

  Header (Image, 4, 4);
  Chunk (Image + 28, 0xcac3, 1, 0);
  Chunk (Image + 40, 0xcac1, 1, 512);
  memset (Image + 52, 0x37, 512);
  Chunk (Image + 564, 0xcac2, 1, 4);
  Put32 (Image + 576, 0x78563412);
  Chunk (Image + 580, 0xcac3, 1, 0);
  Size = 592;
  memset (Disk, 0xa5, sizeof (Disk));
  assert (Flash (Image, Size) == EFI_SUCCESS);
  assert (Disk[0] == 0xa5 && Disk[1536] == 0xa5);
  for (UINTN I = 512; I < 1024; I++) assert (Disk[I] == 0x37);
  for (UINTN I = 1024; I < 1536; I += 4)
    assert (ReadUnaligned32 ((UINT32 *)(Disk + I)) == 0x78563412);
  assert (FastbootWriteImage (&Io, Image, Size, FALSE, NULL)
          == EFI_UNSUPPORTED);

  // Malformed later chunks must not partially write earlier valid chunks.
  for (UINTN N = 4; N < Size; N++) Reject (Image, N);
  memcpy (Bad, Image, Size);
  Put32 (Bad + 588, 11); Reject (Bad, Size);
  memcpy (Bad, Image, Size);
  Put32 (Bad + 16, MAX_UINT32); Reject (Bad, Size);
  memcpy (Bad, Image, Size);
  Put32 (Bad + 24, 1); Reject (Bad, Size);
  memcpy (Bad, Image, Size);
  Put16 (Bad + 580, 0xcac4); Reject (Bad, Size);
  memcpy (Bad, Image, Size);
  Put32 (Bad + 12, 513); Reject (Bad, Size);
  memcpy (Bad, Image, Size);
  Put16 (Bad + 10, 65535); Reject (Bad, Size);
  Reject (Image, Size + 1);

  // A subsequent resparse download must preserve earlier written ranges.
  Header (Image, 4, 2);
  Chunk (Image + 28, 0xcac1, 1, 512);
  memset (Image + 40, 0x66, 512);
  Chunk (Image + 552, 0xcac3, 3, 0);
  assert (Flash (Image, 564) == EFI_SUCCESS);
  assert (Disk[0] == 0x66 && Disk[512] == 0x37 && Disk[1024] == 0x12);

  // Skip beyond 4 GiB without truncating the output byte offset.
  Base = 0x100000000ULL;
  Media.LastBlock = Base / 512 + 1;
  Header (Image, (UINT32)(Base / 512) + 1, 2);
  Chunk (Image + 28, 0xcac3, (UINT32)(Base / 512), 0);
  Chunk (Image + 40, 0xcac1, 1, 512);
  memset (Image + 52, 0x44, 512);
  assert (Flash (Image, 564) == EFI_SUCCESS && Disk[0] == 0x44);
  Base = 0;
  Media.LastBlock = sizeof (Disk) / 512 - 1;

  // Native 4 KiB namespace sectors and raw tail padding.
  memset (Image, 0x24, sizeof (Image));
  Media.BlockSize = 4096;
  Media.LastBlock = sizeof (Disk) / 4096 - 1;
  assert (Flash (Image, sizeof (Image)) == EFI_SUCCESS);
  assert (Disk[2047] == 0x24 && Disk[2048] == 0xff);
  BadRead = TRUE;
  assert (Flash (Image, 512) == EFI_CRC_ERROR);
  BadRead = FALSE; BadWrite = TRUE;
  assert (Flash (Image, 512) == EFI_DEVICE_ERROR && Reads == 0);
  BadWrite = FALSE; BadFlush = TRUE;
  assert (Flash (Image, 512) == EFI_DEVICE_ERROR && Reads == 0);
  BadFlush = FALSE; Media.ReadOnly = TRUE;
  assert (Flash (Image, 512) == EFI_WRITE_PROTECTED && Writes == 0);
  Media.ReadOnly = FALSE;
  UINT8 *Large = malloc (3 * SIZE_64KB);
  memset (Large, 0x19, 3 * SIZE_64KB);
  CancelStatus = EFI_ABORTED;
  Writes = Reads = Flushes = ProgressCalls = 0;
  assert (FastbootWriteImage (&Io, Large, 3 * SIZE_64KB, TRUE, Cancel)
          == EFI_ABORTED);
  assert (Writes == 1 && Reads == 0 && Flushes == 1);
  assert (ProgressCalls == 2);
  CancelVerify = TRUE;
  CancelStatus = EFI_MEDIA_CHANGED;
  Writes = Reads = Flushes = ProgressCalls = 0;
  assert (FastbootWriteImage (&Io, Large, 3 * SIZE_64KB, TRUE, Cancel)
          == EFI_MEDIA_CHANGED);
  assert (Writes == 3 && Reads == 1 && Flushes == 1);
  free (Large);
  Writes = Reads = Flushes = Completions = 0;
  assert (FastbootWriteImage (&Io, Image, 513, TRUE, RecordCompletion)
          == EFI_SUCCESS && Completions == 3);
  Writes = Reads = Flushes = Completions = 0;
  BadRead = TRUE;
  assert (FastbootWriteImage (&Io, Image, 513, TRUE, RecordCompletion)
          == EFI_CRC_ERROR && Completions == 1);
  puts ("Fastboot bounds, resparse, verification and cancellation passed");
  return 0;
}
