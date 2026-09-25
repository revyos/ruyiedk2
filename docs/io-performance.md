# TH1520 firmware I/O performance

Measurements use a Meles 16 GiB board, coreboot with AON at 1848 MHz,
and the RELEASE EDK2 payload. Rates below use MiB/s, except flash clock
frequencies. No display or peripheral PLL is retuned by these changes.

## Storage and USB

| Test | Previous | Updated |
| --- | --- | --- |
| eMMC, 64 MiB reads in 128 KiB requests | 11.5 MiB/s | 41.4–41.8 MiB/s |
| SPI, roughly 801 kB compressed payload | 4867 ms | 647 ms |
| SPI, 106433-byte OpenSBI payload | 646 ms | 86 ms |
| SPI, write and verify a 2 MiB image | 52.23 s | 26.94 s |
| USB flash drive, 64 MiB reads | 34.5 MiB/s | 37.9 MiB/s |

The eMMC driver negotiates HS52 within the card and DTB limits, with a
49.5 MHz actual clock and eight data bits on this board. ADMA2 uses
private pages below 4 GiB, so unaligned or high-address caller buffers
remain supported. Descriptors never cross a 128 MiB DWC MSHC boundary.
PIO remains available if DMA allocation or controller support is absent.
The Linux root filesystem boots normally after the UEFI handoff.

Coreboot and EDK2's SPI writer derive their divider from the current
VIDEO_PLL. Divider 80 produces 9.9 MHz instead of the old 1.98 MHz from
/400. FIFO bursts keep the bus busy without overflowing the receive
FIFO. The board's existing 10 MHz flash limit remains in force.

The USB drive is a 1f75:0918 USB 3.x device behind the Meles GL3523 hub.
Its Linux direct-read reference is about 73 MiB/s. EDK2's synchronous BOT
path is still slower; these measurements do not imply USB line-rate
throughput. The adapter avoids repeated common-buffer cache sweeps for
ERDP credit updates, retaining synchronization before doorbells and
USBSTS-based completion checks.

Both 64 MiB read passes agree, including the chunk-CRC fingerprints
(eMMC `dd2b622e`, USB `3ed9af95` in the tested disk state). Small reads
through unaligned caller buffers also pass. Tests do not write either
storage device.

## Ethernet

The PHY remains at gigabit/full duplex with a 125 MHz RGMII clock.
Eight independent TX descriptors replace the single outstanding frame,
and RX capacity increases from 16 to 64 descriptors. Caller buffers are
recycled in submission order, including after a quiesced controller reset.

The original sequential SNP internal-loopback test verified 16384 frames
at 12.6 MiB/s. An eight-frame batch test with the new queue verifies the
same frame count at 21.1 MiB/s. This comparison includes the changed
submission pattern; it is not a claim of measured Ethernet line rate.
The UEFI network stack also acquires a DHCP lease over the physical
Ethernet connection. Ten 1472-byte pings to the test host all succeed
with zero loss. No external bulk-transfer line-rate claim is made.

## Reproducing the tests

```sh
make check
make edk2-io-test
make edk2-net-test
```

The standalone applications are emitted under
`build/Build/Meles/RELEASE_GCC/RISCV64/`. They are not included in the
production firmware volume.

`IoTest.efi` performs two read-only passes over the first 64 MiB of each
whole-disk Block I/O device. It waits for asynchronous USB enumeration
after a Fastboot-to-host role change and connects discovered interfaces.
Time excludes CRC calculation, so compare it with an uncached read test.
The fingerprint is a CRC of the 512 per-request CRC values.

`NetTest.efi` is specific to Meles GMAC0. It selects the physical MAC-only
handle, excluding PXE aliases, disconnects upper network consumers and
uses DWMAC internal loopback through the SNP API. It checks payloads and
recycled buffer order, restores the MAC configuration, quiesces the test
traffic and reconnects consumers. Internal loopback measures the driver
and DMA path; it is not a measurement through a switch or a remote peer.
