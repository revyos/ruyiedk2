// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../platforms/Th1520Pkg/Drivers/UsbHostDxe/UsbHost.c"
#include <assert.h>
#include <stdio.h>

static unsigned syncs, accesses;
VOID UsbDmaSync (VOID) { syncs++; }
VOID EFIAPI MemoryFence (VOID) {}
static EFI_STATUS EFIAPI
Access (EFI_PCI_IO_PROTOCOL *Pci, EFI_PCI_IO_PROTOCOL_WIDTH Width,
        UINT8 Bar, UINT64 Offset, UINTN Count, VOID *Buffer)
{
  accesses++;
  return EFI_SUCCESS;
}
int main (void)
{
  UINT32 Value = 0;
  EventDequeue = 0x1038;
  SavedRead = Access;
  SavedWrite = Access;
  ReadRegisters (NULL, EfiPciIoWidthUint32, 0, 0x24, 1, &Value);
  assert (syncs == 1); // USBSTS precedes event-ring inspection.
  WriteRegisters (NULL, EfiPciIoWidthUint32, 0, 0x800, 1, &Value);
  assert (syncs == 2); // Doorbell publishes descriptor writes.
  for (UINTN Offset = EventDequeue; Offset <= EventDequeue + 4; Offset += 4)
  {
    ReadRegisters (NULL, EfiPciIoWidthUint32, 0, Offset, 1, &Value);
    WriteRegisters (NULL, EfiPciIoWidthUint32, 0, Offset, 1, &Value);
  }
  assert (syncs == 2 && accesses == 6);
  ReadRegisters (NULL, EfiPciIoWidthUint64, 0, EventDequeue, 1, &Value);
  ReadRegisters (NULL, EfiPciIoWidthUint32, 1, EventDequeue, 1, &Value);
  ReadRegisters (NULL, EfiPciIoWidthUint32, 0, EventDequeue, 2, &Value);
  assert (syncs == 5 && accesses == 9);
  puts ("USB MMIO: status/doorbell sync retained; ERDP skips redundant sync");
}
