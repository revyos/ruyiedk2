// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FastbootStorageLib.h>
#include <Library/MemoryAllocationLib.h>

// Android libsparse wire format; all fields are little-endian.
#define SPARSE_MAGIC   0xed26ff3aU
#define HEADER_BYTES   28
#define CHUNK_BYTES    12
#define CHUNK_RAW      0xcac1
#define CHUNK_FILL     0xcac2
#define CHUNK_SKIP     0xcac3
#define CHUNK_CRC32    0xcac4
#define IO_BYTES       SIZE_64KB

typedef struct
{
  FASTBOOT_STORAGE_PROGRESS Progress;
  UINT64 Done, Total;
} TRANSFER_PROGRESS;

STATIC UINT16
Get16 (CONST UINT8 *P)
{
  return ReadUnaligned16 ((CONST UINT16 *)P);
}

STATIC UINT32
Get32 (CONST UINT8 *P)
{
  return ReadUnaligned32 ((CONST UINT32 *)P);
}

STATIC EFI_STATUS
ValidateSparse (CONST UINT8 *Data, UINTN Size, UINT32 Sector,
                UINT64 Capacity, UINT64 *WriteBytes)
{
  UINT32 BlockSize, Blocks, Chunks;
  UINTN Header, ChunkHeader, Offset;
  UINT64 Remaining;

  *WriteBytes = 0;
  if (Size < HEADER_BYTES || Get16 (Data + 4) != 1)
    return EFI_COMPROMISED_DATA;
  Header = Get16 (Data + 8);
  ChunkHeader = Get16 (Data + 10);
  BlockSize = Get32 (Data + 12);
  Blocks = Get32 (Data + 16);
  Chunks = Get32 (Data + 20);
  if (Header < HEADER_BYTES || Header > Size || ChunkHeader < CHUNK_BYTES
      || !BlockSize || BlockSize % Sector || BlockSize % 4 || !Blocks)
    return EFI_COMPROMISED_DATA;
  if ((UINT64)Blocks * BlockSize > Capacity)
    return EFI_BAD_BUFFER_SIZE;
  // Fastboot resparse streams omit CRCs. Reject unsupported checksums.
  if (Get32 (Data + 24))
    return EFI_UNSUPPORTED;
  Offset = Header;
  Remaining = Blocks;
  for (UINT32 I = 0; I < Chunks; I++)
  {
    UINT32 Count, Total;
    UINT64 Payload;
    UINT16 Type;

    if (Size - Offset < ChunkHeader)
      return EFI_COMPROMISED_DATA;
    Type = Get16 (Data + Offset);
    Count = Get32 (Data + Offset + 4);
    Total = Get32 (Data + Offset + 8);
    if (Count > Remaining || Total < ChunkHeader || Total > Size - Offset)
      return EFI_COMPROMISED_DATA;
    Payload = Total - ChunkHeader;
    switch (Type)
    {
    case CHUNK_RAW:
      if (!Count || Payload != (UINT64)Count * BlockSize)
        return EFI_COMPROMISED_DATA;
      break;
    case CHUNK_FILL:
      if (!Count || Payload != 4)
        return EFI_COMPROMISED_DATA;
      break;
    case CHUNK_SKIP:
      if (Payload)
        return EFI_COMPROMISED_DATA;
      break;
    case CHUNK_CRC32:
      return EFI_UNSUPPORTED;
    default:
      return EFI_COMPROMISED_DATA;
    }
    if (Type != CHUNK_SKIP)
      *WriteBytes += (UINT64)Count * BlockSize;
    Remaining -= Count;
    Offset += Total;
  }
  return Remaining == 0 && Offset == Size ? EFI_SUCCESS
                                          : EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
Transfer (EFI_BLOCK_IO_PROTOCOL *Io, UINT64 Offset, CONST UINT8 *Data,
          UINT64 Size, BOOLEAN Fill, BOOLEAN Verify, UINT8 *Expected,
          UINT8 *Actual, TRANSFER_PROGRESS *Progress)
{
  UINTN Sector = Io->Media->BlockSize;
  EFI_STATUS Status;

  if (Fill)
    SetMem32 (Expected, IO_BYTES, Get32 (Data));
  while (Size)
  {
    UINTN Bytes = (UINTN)MIN (Size, IO_BYTES);
    UINTN Padded = ALIGN_VALUE (Bytes, Sector);

    if (Progress->Progress != NULL)
    {
      Status = Progress->Progress (Progress->Done, Progress->Total,
                                    Verify);
      if (EFI_ERROR (Status))
        return Status;
    }
    if (!Fill)
    {
      SetMem (Expected, Padded, 0xff);
      CopyMem (Expected, Data, Bytes);
    }
    if (Verify)
    {
      Status = Io->ReadBlocks (Io, Io->Media->MediaId, Offset / Sector,
                               Padded, Actual);
      if (!EFI_ERROR (Status) && CompareMem (Expected, Actual, Padded))
        Status = EFI_CRC_ERROR;
    }
    else
      Status = Io->WriteBlocks (Io, Io->Media->MediaId, Offset / Sector,
                                Padded, Expected);
    if (EFI_ERROR (Status))
      return Status;
    if (!Fill)
      Data += Bytes;
    Offset += Bytes;
    Size -= Bytes;
    Progress->Done += Bytes;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
TransferSparse (EFI_BLOCK_IO_PROTOCOL *Io, CONST UINT8 *Data,
                BOOLEAN Verify, UINT8 *Expected, UINT8 *Actual,
                TRANSFER_PROGRESS *Progress)
{
  UINTN Offset = Get16 (Data + 8);
  UINTN ChunkHeader = Get16 (Data + 10);
  UINT32 BlockSize = Get32 (Data + 12);
  UINT64 Position = 0;
  EFI_STATUS Status;

  // Validation of every chunk completes before the first storage write.
  for (UINT32 I = 0; I < Get32 (Data + 20); I++)
  {
    UINT16 Type = Get16 (Data + Offset);
    UINT64 Bytes = (UINT64)Get32 (Data + Offset + 4) * BlockSize;

    if (Type != CHUNK_SKIP)
    {
      Status = Transfer (Io, Position, Data + Offset + ChunkHeader,
                          Bytes, Type == CHUNK_FILL, Verify,
                          Expected, Actual, Progress);
      if (EFI_ERROR (Status))
        return Status;
    }
    Position += Bytes;
    Offset += Get32 (Data + Offset + 8);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
FastbootWriteImage (EFI_BLOCK_IO_PROTOCOL *Io, CONST VOID *Data,
                    UINTN Size, BOOLEAN AllowSparse,
                    FASTBOOT_STORAGE_PROGRESS Progress)
{
  EFI_BLOCK_IO_MEDIA *Media;
  UINT64 Capacity;
  UINT8 *Expected, *Actual;
  EFI_STATUS Status;
  BOOLEAN Sparse;
  TRANSFER_PROGRESS TransferProgress = { Progress, 0, Size };

  if (Io == NULL || Io->Media == NULL || Data == NULL || Size == 0)
    return EFI_INVALID_PARAMETER;
  Media = Io->Media;
  if (!Media->MediaPresent)
    return EFI_NO_MEDIA;
  if (Media->ReadOnly)
    return EFI_WRITE_PROTECTED;
  if (!Media->BlockSize || IO_BYTES % Media->BlockSize
      || Media->BlockSize % 4 || Media->IoAlign > EFI_PAGE_SIZE
      || Media->LastBlock >= MAX_UINT64 / Media->BlockSize)
    return EFI_UNSUPPORTED;
  Capacity = (Media->LastBlock + 1) * Media->BlockSize;
  Sparse = Size >= 4 && Get32 (Data) == SPARSE_MAGIC;
  if (Sparse)
  {
    if (!AllowSparse)
      return EFI_UNSUPPORTED;
    Status = ValidateSparse (Data, Size, Media->BlockSize, Capacity,
                              &TransferProgress.Total);
    if (EFI_ERROR (Status))
      return Status;
  }
  else if (Size > Capacity)
    return EFI_BAD_BUFFER_SIZE;
  Expected = AllocatePages (EFI_SIZE_TO_PAGES (IO_BYTES));
  Actual = AllocatePages (EFI_SIZE_TO_PAGES (IO_BYTES));
  if (Expected == NULL || Actual == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }
  for (UINTN Pass = 0; Pass < 2; Pass++)
  {
    TransferProgress.Done = 0;
    if (Sparse)
      Status = TransferSparse (Io, Data, Pass != 0, Expected, Actual,
                                &TransferProgress);
    else
      Status = Transfer (Io, 0, Data, Size, FALSE, Pass != 0,
                          Expected, Actual, &TransferProgress);
    if (EFI_ERROR (Status))
    {
      // Leave completed writes durable even when the caller cancels.
      if (Pass == 0)
        Io->FlushBlocks (Io);
      goto Out;
    }
    if (Pass == 0)
    {
      Status = Io->FlushBlocks (Io);
      if (EFI_ERROR (Status))
        goto Out;
    }
    if (Progress != NULL)
    {
      Status = Progress (TransferProgress.Total, TransferProgress.Total,
                          Pass != 0);
      if (EFI_ERROR (Status))
        goto Out;
    }
  }
Out:
  if (Expected != NULL)
    FreePages (Expected, EFI_SIZE_TO_PAGES (IO_BYTES));
  if (Actual != NULL)
    FreePages (Actual, EFI_SIZE_TO_PAGES (IO_BYTES));
  return Status;
}
