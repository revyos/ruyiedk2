// SPDX-License-Identifier: BSD-2-Clause-Patent
#define MDEPKG_NDEBUG
#include "../platforms/Th1520Pkg/Drivers/GmacDxe/Gmac.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UINT32 Registers[2048];
EFI_BOOT_SERVICES *gBS;
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N) { return memcpy (D, S, N); }
VOID *EFIAPI ZeroMem (VOID *D, UINTN N) { return memset (D, 0, N); }
VOID *EFIAPI WriteBackInvalidateDataCacheRange (VOID *P, UINTN N) { return P; }
VOID *EFIAPI InvalidateDataCacheRange (VOID *P, UINTN N) { return P; }
UINT64 EFIAPI GetPerformanceCounter (VOID) { return 0; }
UINTN EFIAPI MicroSecondDelay (UINTN N) { return N; }
VOID EFIAPI EfiAcquireLock (EFI_LOCK *L) {}
VOID EFIAPI EfiReleaseLock (EFI_LOCK *L) {}
LIST_ENTRY *EFIAPI GetFirstNode (CONST LIST_ENTRY *L) { return L->ForwardLink; }
LIST_ENTRY *EFIAPI GetNextNode (CONST LIST_ENTRY *L, CONST LIST_ENTRY *N) { return N->ForwardLink; }
BOOLEAN EFIAPI IsNull (CONST LIST_ENTRY *L, CONST LIST_ENTRY *N) { return L == N; }
UINT32 EFIAPI MmioRead32 (UINTN A) { return Registers[A / 4]; }
UINT32 EFIAPI MmioWrite32 (UINTN A, UINT32 V)
{
  Registers[A / 4] = A == DMA_STATUS ? 0 :
      (A == DMA_BUS_MODE ? V & ~BIT0 : V);
  return V;
}
EFI_STATUS GmacSetSpeed (GMAC_HARDWARE *H, UINTN S) { return EFI_SUCCESS; }
EFI_STATUS GmacLink (GMAC_HARDWARE *H, BOOLEAN *Up, UINTN *Speed, BOOLEAN *Duplex)
{
  *Up = *Duplex = TRUE;
  *Speed = 1000;
  return EFI_SUCCESS;
}
int main (void)
{
  GMAC_DEVICE Nic = {0};
  GMAC_DMA Dma = {0};
  UINT8 Buffers[TX_COUNT + 1][64] = {{0}};
  VOID *Cookie;
  Nic.Dma = &Dma;
  Nic.Mode.State = EfiSimpleNetworkInitialized;
  Nic.Mode.MediaPresent = TRUE;
  Nic.NextLinkCheck = MAX_UINT64;
  Devices.ForwardLink = Devices.BackLink = &Nic.Link;
  Nic.Link.ForwardLink = Nic.Link.BackLink = &Devices;
  assert (InitializeHardware (&Nic) == EFI_SUCCESS);
  Nic.NextLinkCheck = MAX_UINT64;
  for (UINTN Round = 0; Round < 3; Round++)
  {
    for (UINTN I = 0; I < TX_COUNT; I++)
    {
      assert (Transmit (&Nic.Snp, 0, 64, Buffers[I], NULL, NULL, NULL) == 0);
    }
    assert (Nic.TxPending == TX_COUNT);
    assert (Transmit (&Nic.Snp, 0, 64, Buffers[TX_COUNT], NULL, NULL, NULL) ==
            EFI_NOT_READY);
    Cookie = (VOID *)1;
    assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0 && Cookie == NULL);
    // Complete out of order: the caller must still get FIFO recycling.
    Dma.Tx[(Nic.TxTail + 1) % TX_COUNT].Status &= ~DESC_OWN;
    assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0 && Cookie == NULL);
    for (UINTN I = 0; I < TX_COUNT; I++)
    {
      Dma.Tx[Nic.TxTail].Status &= ~DESC_OWN;
      assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0);
      assert (Cookie == Buffers[I]);
    }
    assert (Nic.TxPending == 0);
  }
  // Reset must retain every caller cookie after quiescing the old DMA.
  for (UINTN I = 0; I < 3; I++)
  {
    assert (Transmit (&Nic.Snp, 0, 64, Buffers[I], NULL, NULL, NULL) == 0);
  }
  assert (Reset (&Nic.Snp, FALSE) == EFI_SUCCESS);
  assert (Transmit (&Nic.Snp, 0, 64, Buffers[3], NULL, NULL, NULL) == 0);
  for (UINTN I = 0; I < 3; I++)
  {
    assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0 && Cookie == Buffers[I]);
  }
  assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0 && Cookie == NULL);
  Dma.Tx[Nic.TxTail].Status &= ~DESC_OWN;
  assert (GetStatus (&Nic.Snp, NULL, &Cookie) == 0 && Cookie == Buffers[3]);
  assert (Nic.TxPending == 0);
  puts ("GMAC TX queue: backpressure, wrap, ordered recycle and reset passed");
}
