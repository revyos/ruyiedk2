// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/Th1520Pkg/Library/SpiLib/Spi.c"
#include "../platforms/Th1520Pkg/Library/SpiLib/RuntimeIo.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UINT32 Registers[64], Gate = BIT17;
static unsigned Writes, GpioWrites, Sent, Received;
static UINT8 Command;
static BOOLEAN RuntimeOnly;
static UINT64 Counter;
static unsigned RuntimeAccesses;
static _Alignas (4096) UINT64 Pages[512 * TH1520_SPI_RUNTIME_PAGES];
UINT32 EFIAPI MmioRead32 (UINTN Address)
{
  if (Address == AP + 0x204) return Gate;
  if (Address == AP + 0x30) return 1 | (99 << 8) | (3 << 20) | BIT24;
  if (Address == AP + 0x34) return BIT24;
  if (Address >= SPI && Address < SPI + sizeof (Registers))
  {
    UINTN Offset = Address - SPI;
    if (Offset == 0x18) return MIN (Registers[Offset / 4], 31);
    if (Offset == 0x24) return Sent - Received;
    if (Offset == 0x28) return 0;
    if (Offset == 0x60)
    {
      static const UINT8 Id[] = {0, 0xef, 0x60, 0x17};
      unsigned Index = Received++;
      if (Command == 5 && Index == 1) return 2;
      return Command == 0x9f && Index < 4 ? Id[Index] : 0;
    }
    return Registers[Offset / 4];
  }
  assert (!RuntimeOnly);
  return 0;
}
UINT32 EFIAPI MmioWrite32 (UINTN Address, UINT32 Value)
{
  Writes++;
  if (Address == GPIO || Address == GPIO + 4) GpioWrites++;
  if (RuntimeOnly) assert (Address >= SPI && Address < SPI + 0x1000);
  if (Address >= SPI && Address < SPI + sizeof (Registers))
  {
    UINTN Offset = Address - SPI;
    if (NativeCs && Offset == 0x10 && Value)
      assert (Sent && Sent <= 32);
    Registers[Offset / 4] = Value;
    if (Offset == 8 && !Value) Sent = Received = 0;
    if (Offset == 0x60)
    {
      if (NativeCs)
      {
        assert (!Registers[0x10 / 4]);
        assert (Sent < 32);
      }
      if (!Sent) Command = Value;
      Sent++;
    }
  }
  return Value;
}
UINT32 EFIAPI MmioOr32 (UINTN A, UINT32 V)
{ return MmioWrite32 (A, MmioRead32 (A) | V); }
UINT32 EFIAPI MmioAnd32 (UINTN A, UINT32 V)
{ return MmioWrite32 (A, MmioRead32 (A) & V); }
UINT32 EFIAPI MmioAndThenOr32 (UINTN A, UINT32 M, UINT32 V)
{ return MmioWrite32 (A, (MmioRead32 (A) & M) | V); }
UINT64 EFIAPI GetPerformanceCounter (VOID) { return ++Counter; }
UINT64 EFIAPI GetTimeInNanoSecond (UINT64 T) { return T * 1000; }
UINTN EFIAPI MicroSecondDelay (UINTN T) { return T; }
UINT64 EFIAPI LShiftU64 (UINT64 V, UINTN N) { return V << N; }
UINT64 EFIAPI RShiftU64 (UINT64 V, UINTN N) { return V >> N; }
UINT64 EFIAPI MultU64x32 (UINT64 V, UINT32 N) { return V * N; }
UINT64 EFIAPI DivU64x32 (UINT64 V, UINT32 N) { return V / N; }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N)
{ return memcpy (D, S, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N)
{ return memcmp (A, B, N); }
VOID *EFIAPI SetMem (VOID *P, UINTN N, UINT8 V)
{ return memset (P, V, N); }
VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset (P, 0, N); }
VOID *EFIAPI WriteBackDataCacheRange (VOID *P, UINTN N)
{ assert (P == Pages && N == sizeof (Pages)); return P; }
UINT32 EFIAPI Th1520SpiMmio (
    UINTN Address, UINT32 Value, UINTN Satp, BOOLEAN Write)
{
  assert (Satp == (BIT63 | ((UINTN)Pages >> 12)));
  UINT64 *Table = Pages;
  for (int Level = 2; Level > 0; Level--)
  {
    UINT64 Entry = Table[(Address >> (12 + Level * 9)) & 511];
    assert ((Entry & 0x3ff) == 1);
    Table = (UINT64 *)(UINTN)((Entry >> 10) << 12);
  }
  UINT64 Entry = Table[(Address >> 12) & 511];
  assert ((Entry & (BIT63 | BIT62 | BIT61 | BIT60 | 0x3ff)) == PTE_MMIO);
  UINTN Physical = Address >= AP ? 0xffef010000ULL : 0xffea000000ULL;
  assert ((Entry & ~(PTE_DEVICE | 0x3ff)) == ((Physical >> 12) << 10));
  RuntimeAccesses++;
  return Write ? MmioWrite32 (Address, Value) : MmioRead32 (Address);
}
static EFI_STATUS EFIAPI Convert (UINTN Flags, VOID **Address)
{
  if ((UINTN)*Address == SPI)
    *Address = (VOID *)0x201e0000;
  else if ((UINTN)*Address == AP)
    *Address = (VOID *)0x201f0000;
  else
    *Address = (VOID *)(0x20060000 | ((UINTN)*Address & EFI_PAGE_MASK));
  return EFI_SUCCESS;
}
int main (void)
{
  UINT8 Buffer[512] = {0};
  assert (Th1520SpiInitNative () == EFI_SUCCESS);
  assert (NativeCs && !GpioWrites);
  RuntimeOnly = TRUE;
  assert (Th1520SpiRuntimeInit () == EFI_SUCCESS);
  assert (Th1520SpiRead (0x7c0000, sizeof (Buffer), Buffer) ==
          EFI_SUCCESS);
  assert (Th1520SpiProgram (0x7c00f0, sizeof (Buffer), Buffer) ==
          EFI_SUCCESS);
  assert (Exchange (Buffer, Buffer, 33) == EFI_BAD_BUFFER_SIZE);
  assert (Th1520SpiRuntimePrepare (Pages) == EFI_SUCCESS);
  assert (Th1520SpiConvertPointers (Convert) == EFI_SUCCESS);
  assert (Th1520SpiRuntimeInit () == EFI_SUCCESS);
  assert (RuntimeAccesses);
  Gate = 0;
  unsigned Before = Writes;
  assert (Th1520SpiRuntimeInit () == EFI_NOT_READY);
  assert (Writes == Before);
  puts ("SPI runtime: native CS, VA conversion, no GPIO/clock/pad writes");
}
