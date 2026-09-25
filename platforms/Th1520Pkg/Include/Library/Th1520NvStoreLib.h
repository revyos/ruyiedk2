// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <Uefi.h>

#define TH1520_NV_SLOT_SIZE SIZE_128KB
#define TH1520_NV_REGION_SIZE SIZE_256KB
#define TH1520_NV_DATA_SIZE SIZE_64KB
#define TH1520_NV_SPI_OFFSET (SIZE_8MB - TH1520_NV_REGION_SIZE)
#define TH1520_NV_MAGIC SIGNATURE_64 ('T','H','N','V','A','R','S','1')

// Little-endian disk format. Header is committed after data is durable.
#pragma pack(1)
typedef struct {
  UINT64 Magic;
  UINT32 Version, Length;
  UINT64 Generation;
  UINT32 DataCrc, HeaderCrc;
  UINT8 Reserved[480];
} TH1520_NV_HEADER;
#pragma pack()

typedef struct {
  EFI_STATUS (*Read)(UINTN Offset, UINTN Length, VOID *Data);
  EFI_STATUS (*Write)(UINTN Offset, UINTN Length, CONST VOID *Data);
  EFI_STATUS (*Erase)(UINTN Offset, UINTN Length);
  UINTN Offset;
  INTN Slot;
  UINT64 Generation;
  VOID *Scratch;
} TH1520_NV_STORE;

// OVMF SerializeVariablesLib format: name size, UTF-16 name, GUID,
// attributes, data size, data; packed, with no inter-record alignment.
typedef struct {
  UINTN Size, NameSize, DataSize;
  CONST VOID *Name, *Guid, *Data;
  UINT32 Attributes;
} TH1520_NV_RECORD;

EFI_STATUS Th1520NvRecord (
    CONST VOID *Data, UINTN Length, UINTN Offset, TH1520_NV_RECORD *R);
EFI_STATUS Th1520NvValidate (CONST VOID *Data, UINTN Length);
EFI_STATUS Th1520NvUpdate (
    VOID *Buffer, UINTN *Length, CONST CHAR16 *Name, CONST EFI_GUID *Guid,
    UINT32 Attributes, UINTN Size, CONST VOID *Data);
EFI_STATUS Th1520NvLoad (
    TH1520_NV_STORE *Store, VOID *Buffer, UINTN *Length);
EFI_STATUS Th1520NvSave (
    TH1520_NV_STORE *Store, CONST VOID *Buffer, UINTN Length);
