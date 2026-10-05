# Fastboot media access

TH1520 RAM recovery and normal EDK2 provide the same raw media interface:

| Name | Medium | Capacity |
| --- | --- | --- |
| `spi` | Complete SPI NOR | 8 MiB |
| `emmc0` | eMMC user area | Card capacity |
| `emmc0boot0` | First eMMC hardware boot area | Card boot-area capacity |
| `emmc0boot1` | Second eMMC hardware boot area | Card boot-area capacity |

Every name supports both read and write in both profiles. The eMMC targets
are available when the eMMC driver has initialized the card. Use media names
directly; there is no `loader` target.

## Raw writes

```sh
fastboot -s meles-edk2 getvar partition-size:spi
fastboot -s meles-edk2 -S 0 flash spi meles-normal-spi.bin
fastboot -s meles-edk2 -S 0 flash emmc0boot0 boot0.bin
fastboot -s meles-edk2 -S 0 flash emmc0boot1 boot1.bin
fastboot -s meles-edk2 -S 0 flash emmc0 user-area.bin
```

Replace `meles-edk2` with the connected product, such as `meles-recovery`.
Firmware treats downloaded contents as opaque bytes. There is no image,
magic, FIT, CBFS, hash, signature or sparse-container format validation.
Sparse-container bytes are written as supplied, without expansion.
Transport sizes and byte ranges must fit the physical medium.

Raw targets expose the complete capacity, including SPI variables at
`0x7c0000..0x7fffff` and MAC records at the end of eMMC boot0. A 2 MiB
firmware update at SPI offset zero writes only that requested span.
Unaligned writes preserve the bytes outside the requested span through
read/modify/write of the first and last physical blocks. Write readback
checks the programmed bytes; it does not interpret their format.

eMMC access preserves boot-enable/boot-ack bits and restores user-area
selection after success or failure. A failed restore prevents ordinary
Block I/O from accessing a boot area under the user-area interface.

## Reads and byte ranges

Standard reads use `fastboot fetch`:

```sh
fastboot -s meles-edk2 fetch spi spi.bin
fastboot -s meles-edk2 fetch emmc0boot0 boot0.bin
fastboot -s meles-edk2 fetch emmc0boot1 boot1.bin
```

The wire protocol accepts `fetch:MEDIA:OFFSET:LENGTH` and
`flash:MEDIA:OFFSET`; numbers on the wire are hexadecimal. Offsets are
64-bit. One download or fetch is at most 32 MiB. The supplied helper
chunks larger files and sends bytes without Android image processing:

```sh
python3 tools/fastboot-storage.py --serial meles-edk2 \
  read emmc0 emmc-prefix.bin --offset 0 --length 0x100000
python3 tools/fastboot-storage.py --serial meles-edk2 \
  write spi firmware.bin --offset 0
python3 tools/fastboot-storage.py --serial meles-edk2 \
  read spi range.bin --offset 0x300123 --length 4097
```

The helper requires `libusb-1.0`, selects the USB serial explicitly and
uses 16 MiB chunks. Reads with no length cover the remaining medium.
Progress and final success describe only the completed transfer. I/O
errors can leave a partial write; the interface does not provide rollback.

## Validation

Host checks exercise 144 combinations of offsets and lengths with 512-byte
eMMC blocks and 4 KiB SPI sectors. They check byte equality, unchanged
adjacent bytes, sparse magic as ordinary data, final-block access,
64-bit range overflow, backend errors and mismatched write readback.
Target/parser tests cover media names, card capacities and malformed ranges.
These models do not establish physical device or USB behavior.

Build and board receipts are retained separately in the parent workspace.
RAM image boot, normal image boot and physical reads/writes must each be
recorded before claiming a tested firmware release.

On 2026-10-06, Meles v1.3 normal firmware passed physical access to all
four media: 8 MiB SPI, a 123731968000-byte eMMC user area and two 4 MiB
boot areas. Reads matched independent Linux captures. Each medium passed
a 70001-byte unaligned write beginning with sparse magic, a one-byte write,
adjacent-byte checks and restoration to the exact previous contents.
The RAM recovery image builds, but its new BootROM boot test was deferred
because the board could not be switched to download mode during this run.
