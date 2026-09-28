# Melon Pi V1 and V2 (A210 SODIMM)

The native boot chain is coreboot, OpenSBI `FW_DYNAMIC`, then EDK2.
Neither U-Boot nor bootzero runs in this chain. RAM boot leaves eMMC
contents unchanged.

## Layout

`MelonPiPkg` selects the board profile. `A210Pkg` contains the SoC MMIO
resources and eMMC integration. `RiscVPlatformPkg` provides the shared
FDT-based SEC entry, boot manager and openRuyi startup graphics.
The upstream EDK2 tree remains unchanged.

The EDK2 FD is linked at `0x88000000`. SEC temporary memory occupies
`0x84000000..0x85ffffff`, separately from coreboot, its FDT, and the
OpenSBI reservation. Memory capacity comes from the device tree patched
by coreboot after DDR training.

The UART4 console uses 115200 baud, 32-bit MMIO accesses and a 24 MHz
input clock supplied by the BootROM configuration. The A210 CPUs use
standard Zicbom cache maintenance and Svpbmt page attributes.

## Build

From this repository:

```sh
make edk2 EDK2_BOARD=melonpi-v2
```

The result is `build/melonpi-v2/A210.fd`. This is a firmware volume, not
an image to flash directly to eMMC.

Use `melonpi-v1` for the V1 carrier. Both models use the same EDK2
platform and drivers; the kernel-built board DTB supplies the model
and peripheral resources. V3 is a minor V2 hardware revision and uses
the V2 firmware and DTB. The `melonpi` build name remains available for
existing build scripts. Selecting a build
name does not convert an incompatible DTB into the correct board.

From the parent `th1520` directory, package it with the A210 OpenSBI
ELF and the board DTB. First build `zhihe/a210-melonpi-v2.dtb` in the
target kernel and copy it to `build/a210-port/a210-melonpi-v2.dtb`:

Set `MELONPI_OPENSBI` to the path of `fw_dynamic.elf`.

```sh
python3 coreboot/util/zhihe/build-firmware.py \
  --board melonpi-v2 \
  --boot-medium ram \
  --aon build/a210-port/aon/a210-aon.bin \
  --opensbi "$MELONPI_OPENSBI" \
  --edk2-fd th1520-edk2/build/melonpi-v2/A210.fd \
  --dtb build/a210-port/a210-melonpi-v2.dtb \
  --output build/a210-port/melonpi-v2-edk2-ram
```

The DTB must describe the board peripherals, all eight application harts,
and the A210 OpenSBI HSM resources. The packaging command does not build
OpenSBI or the DTB. Supply the A210 E902 firmware image separately.
Use matching `--board`, DTB and output directory names for V1 or V2.
The packager checks the DTB compatible before building either stage.
Carrier revisions are not inferred from the module's ADC memory straps.

V1 uses a Type-C connector for the dedicated USB2 download controller;
V2 uses Type-A. Both models use the same DWC2 download controller.
V1 carriers may omit U53, the MPF52002 Type-C controller, or its PD_5V
supply components. The USB3 port remains disabled by default until
automatic role and orientation control is available. Linux host tests
have verified USB3 on V1 with the independent VBUS switch and a matching
plug orientation. This does not provide automatic Type-C operation.
FANG enclosure startup remains unreliable. The separate Lexar drive
has passed EDK2 transfers and EFI boot in the fixed-host configuration.
eMMC, Ethernet and HDMI use shared drivers. V1 has passed coreboot/EDK2
boot, Fastboot, NVMe and USB tests. The V2 results were obtained on
the minor revision previously called V3.

Coreboot starts AON and uses its voltage RPCs before configuring both
CPU clusters at 1.896 GHz. Linux must attach to the running AON without
reloading it. A working cooling fan or heatsink is required.

## RAM download

Enter BootROM download mode. Download and run the small first stage:

```sh
fastboot -S 0 flash ram \
  build/a210-port/melonpi-v2-edk2-ram/melonpi-v2-bootstrap-rvbl.bin
fastboot reboot
```

Wait for `OCRAM ready for BootROM RAM download` on UART4. Then download
and run the full image:

```sh
fastboot -S 0 flash ram \
  build/a210-port/melonpi-v2-edk2-ram/melonpi-v2-coreboot-ram-rvbl.bin
fastboot reboot
```

Both commands target volatile RAM. The first stage enables the fan and
OCRAM. The second trains DDR and starts coreboot, OpenSBI and EDK2.

## eMMC

The A210 host integrates the upstream SDHCI and eMMC drivers through
`EDKII_SD_MMC_OVERRIDE_PROTOCOL` and the non-discoverable device
protocol. This PCI I/O abstraction does not imply a physical PCI bus.
The DTB supplies the controller address, bus width and frequency limit.
The input clock is decoded from the running PLL and divider registers.

The current mode is 8-bit high-speed SDR with PIO, at approximately
50.77 MHz on the tested BootROM clock configuration. HS200, HS400 and
DMA are not enabled. The peripheral PLL configuration is preserved.

Two controller clock steps are required after the standard driver's
setup: restore SDCLK after card power switching, and set the SDHCI
v4 `PLL_EN` bit before enabling the card clock. Repeat the latter after
frequency updates and wait for clock stability with a timeout. Retain
v4 mode so the driver's 32-bit block count accesses remain consistent.

On Melon Pi, the user area and both 4 MiB boot areas passed comparisons
of 128-block reads against single-block reads at the beginning, at LBA
1, and at the end. Each area also completed a 4 MiB read in 504 ms
(about 7.9 MiB/s). Shell enumerated all nine GPT partitions. These are
bring-up checks, not a full-media stress test or a Linux boot test.

Build the optional read-only check into the firmware with:

```sh
EDK2_BOARD=melonpi tools/build.sh -D BLOCK_READ_TEST
```

It runs when Block I/O becomes available and reports `BlockReadTest`
results on the serial console. It does not write media contents. The
normal build omits this test. `-D EMMC_DEBUG` enables verbose host and
card-driver diagnostics for troubleshooting; omit it when measuring
performance.

## Fastboot in EDK2

`A210.fd` includes a native Fastboot application. Press F9 during the
boot countdown, or select **USB Fastboot** in Boot Manager. Press Esc
or run `fastboot continue` to leave it. This application is separate
from the BootROM service used for the initial RAM download.

The Melon Pi download connector uses the A210 DWC2 USB2 controller.
The transport uses PIO and has been tested at USB high speed. The
application reports protocol version `0.4`, product `Melon Pi EDK2`,
and serial `a210`. Its download buffer is 128 MiB.

Supported storage names:

| Name | Target |
| --- | --- |
| `emmc0` | Entire eMMC user area, including its partition table |
| `emmc0boot0` | Raw first eMMC boot area (Linux boot0) |
| `emmc0boot1` | Raw second eMMC boot area (Linux boot1) |
| `nvme0n1` | NVMe controller 0, namespace 1, including its GPT |

Storage sizes come from Block I/O, not from an assumed eMMC capacity.
Flash writes are bounded to the selected area, padded to a sector with
`0xff`, flushed, and read back before returning success. The boot
partition selection is preserved. The eMMC names map directly to hardware
areas. NVMe controllers are
numbered by ascending PCI segment/bus/device/function, with the hardware
namespace ID after `n`. The console prints their PCI addresses and sizes.
Only whole namespaces are exposed, not GPT partitions. Numbering follows
this firmware's discovery and need not match Linux when controllers change.

Example commands:

```sh
fastboot getvar product
fastboot getvar partition-size:emmc0boot0
fastboot fetch emmc0boot1 boot1-read.bin
fastboot -S 0 flash emmc0boot0 complete-boot0-image.bin
fastboot continue
```

Use a complete image built for eMMC boot0 when updating `emmc0boot0`.
`A210.fd` is an EDK2 payload, and the OCRAM RVBL image is a RAM loader;
neither is an eMMC boot0 image. Boot areas accept raw images only; use
`-S 0` for raw firmware files.

For a full GPT operating-system image, select a user disk:

```sh
fastboot getvar partition-size:emmc0
fastboot getvar partition-size:nvme0n1
fastboot flash emmc0 openruyi-rva20.img
# Or install the same image on the NVMe namespace:
fastboot flash nvme0n1 openruyi-rva20.img
```

These commands replace the selected disk's partitions and data. They do
not write the eMMC hardware boot areas. The host automatically splits
large raw files into Android sparse downloads within the 128 MiB limit.
Do not use `-S 0` for disk images larger than that limit. Filesystem holes
in the host file do not change its raw disk format;
no manual img2simg conversion is needed.

User disks accept sparse v1 RAW, FILL and DONT_CARE chunks. All chunk
bounds and the expanded image size are checked before any write in each
download. DONT_CARE preserves existing sectors, including earlier sparse
downloads. Nonzero header checksums and CRC32 chunks are rejected before
writing; standard fastboot resparse downloads omit these optional checks.
Writes are flushed, then written ranges are read back and compared. The
complete multi-download operation is not atomic. Restart the full flash
command if it is interrupted, and reboot before using the new GPT.

Writing and verification report progress through standard Fastboot INFO
responses and the serial port. Serial output shows the target, phase,
completed MiB and percentage on one updating line. Progress does not
redraw the HDMI console. Counters cover the current download; the host
reports the full-image chunk count. Esc cancels at the next block-transfer
boundary and leaves Fastboot available. USB reset/disconnect or a response timeout stops the
operation and restarts the USB session, invalidating the old download.
A download stalled for five seconds is discarded and the USB session
is restarted as well. Host Ctrl+C has no protocol-level cancel message;
periodic responses detect a vanished host through the transport timeout. Cancellation
may leave partial disk contents and cannot interrupt an active Block I/O
call before that driver completes or times out. On V1, a host SIGINT test
recovered a product query in 7.8 seconds; serial Esc recovered it in
2.5 seconds. Neither test required a board reset.

To execute a PE/COFF EFI application from RAM:

```sh
fastboot stage application.efi
fastboot oem boot-efi
```

The standard client's `fastboot boot FILE` command creates an Android
boot image header for a bare EFI file. The two commands above preserve
the original EFI image. Staging and executing an application do not
write eMMC.

Hardware validation covered F9 entry, menu re-entry, queries, a 4 MiB
fetch, a 1 MiB plus 3-byte boot1 flash, sector padding, write readback,
capacity and read-range rejection, EFI execution, and `continue`.
Boot1 was restored and compared byte-for-byte with its original content.
The complete coreboot boot0 image was flashed through `emmc0boot0`.
After a global reset, the eMMC boot0 chain reached EDK2 with AON
running and both CPU clusters measured at 1.896 GHz.

V1 also passed full disk-image flashing and boot tests with coreboot
26.09 and Linux 7.3-rc4. Separate 4 GiB openRuyi images with distinct
filesystem and GPT UUIDs were written to `emmc0` and `nvme0n1`. Before
OS startup, SHA-256 of each disk's first 4 GiB matched its source image.

Each device was then selected explicitly in Boot Menu. GRUB temporarily
received `console=ttyS4,115200 earlycon`; the generic image defaults were
kept unchanged. Both systems reached multi-user.target, accepted serial
login and reported CPUs 0-7 online and systemd state `running`. The root
mounts were `mmcblk0p2` and `nvme0n1p2`, respectively. First boot expanded
the filesystems to approximately 57 GiB on eMMC and 469 GiB on NVMe.

## Setup information and boot options

Setup shows the board model from the runtime FDT, the A210 application
CPU topology, the firmware version and the installed RAM capacity.
The memory information comes from SEC's FDT-derived resource HOBs after
coreboot DDR training; it does not assume that every board has 8 GiB.
The current board reports `Melon Pi`, `4 x C920v2 + 4 x C908`, and
`8192 MB RAM`. E902 is a management core, not a UEFI application hart.

The frequency shown is the boot hart's current clock, read from the
CPU PLL, clock mux and CCU divider. With the current coreboot image,
Setup shows 1.89 GHz for the 1.896 GHz C908 clock. Reporting the clock
does not change the voltage or frequency.

The standard platform boot-manager protocol excludes physical eMMC
boot0 and boot1 areas from automatically generated boot options. It
runs on every refresh, including refreshes initiated by the menus.
The user area remains bootable. Explicit user-created boot options are
preserved. Both boot areas retain their Block I/O handles for Fastboot
and low-level diagnostics; filtering is not an access restriction.

## Current scope

The serial console provides the standard firmware setup and UEFI Shell.
Enter skips the ten-second boot countdown, F2 opens setup, F9 starts
Fastboot, and F12 opens the boot menu. Variables are volatile.

Persistent variables are not yet implemented for A210. System reset
uses the standard RISC-V SBI library; the A210
OpenSBI reset provider must be included in the complete firmware.
Linux SBI reboot has been tested; EDK2 ResetSystem and shutdown still
need independent validation. SMBIOS reports board,
processor and memory-range information; physical DRAM package inventory
and unique system identifiers are not yet populated.

## NVMe and USB boot

PCIe uses the standard EDK2 host bridge, PCI bus and NVMe drivers.
The A210 libraries initialize the E16 PHY and root ports, then translate
configuration accesses through DesignWare iATU windows. Enabled DT nodes
supply lanes, link speed, bus ranges, address apertures, pins and power
supplies. ASPM remains disabled, matching the A210 Linux port.

SEC derives the CPU HOB I/O width from the enabled PCI ranges. A210's
translated port-I/O aperture is above 4 GiB and needs 37 address bits.
This describes the CPU-side aperture; it does not resize device BARs.
The root bridge permits DMA above 4 GiB, without a global DMA32 limit.

An early A210 driver publishes the CPU mappings' cache types in GCD,
sets DMA alignment to the cache block size and flushes RAM before an
uncached mapping replaces a cacheable mapping. The standard EDK2 DMA
libraries then handle cache maintenance and unaligned bounce buffers.
This uses Zicbom and Svpbmt, with no private SBI service or modification
to upstream EDK2. The software IOMMU protocol here provides DMA mapping;
it does not enable the A210 hardware IOMMU.

USB-A uses a reusable DWC2 host driver with A210 platform initialization.
USB3 fixed-host nodes use the standard non-discoverable xHCI driver.
The USB bus, mass-storage, keyboard and mouse drivers are included.
USB keyboards join the serial console through the standard USB class
device path. The download controller remains available for Fastboot.
The DWC2 reset handshake is completed at ExitBootServices so Linux can
identify and initialize the controller again.

V1 hardware validation covered a WDC SN730 at PCIe Gen3 x4 and a Lexar
USB flash drive at 480 Mb/s. Each passed a 16 MiB write, read, comparison
and restoration check using buffers not aligned to a cache block.
Both media loaded an EFI Linux 7.3-rc4 test image and reached BusyBox.
Linux also enumerated the Logitech receiver after the DWC2 handoff.
Physical keyboard input, SuperSpeed transfers and other carrier revisions
still need separate validation. These tests do not establish automatic
Type-C orientation or role switching.

## Ethernet

The firmware exposes both DT-enabled A210 DWMAC 5.40a controllers as
SNP interfaces. Controller addresses, PHY addresses, RGMII delay mode
and pad groups come from the DTB. A210 clock/reset and pin configuration
live in `EqosPlatformLib`; the common `DwMac4Dxe` driver owns descriptors,
SNP and RTL8211 PHY access. TH1520 keeps its DWMAC 3.70a driver.

Coreboot must initialize the PERI1 IOPMP entries before entering EDK2.
Without them, MDIO and link negotiation work but DMA reports bus errors.
The V2 DTB must enable both controllers and include their RGMII groups;
the original target-kernel DTB only enabled GMAC0. Build the updated
`zhihe/a210-melonpi-v2.dtb` from the target kernel and use it when
packaging.

The network stack provides IPv4, DHCP, DNS and PXE. In UEFI Shell, use
`ifconfig -l` to find the connected port, then, for example:

```text
ifconfig -s eth1 dhcp
ifconfig -l eth1
ping -n 10 -l 1472 192.168.10.1
```

A valid `mac-address` takes precedence over `local-mac-address`.
Invalid values are ignored. This port has no verified A210 eFuse MAC
provider. If neither DT property is valid, EDK2 generates a local unicast
base address, with an even last byte, and saves it in eMMC boot0.
GMAC1 uses the adjacent address. Both runtime DT properties are updated
to the selected address. The console identifies generated addresses and
reports whether saving succeeded.

The last 64 KiB of a 4 MiB boot0 is reserved. A versioned, CRC-protected
512-byte record starts at `0x3f0000`; writes are flushed and read back.
An existing valid record is reused without a write. The coreboot
packager rejects images overlapping this reservation. Flashing a
packaged image only writes its sectors and leaves the record intact.
Raw `emmc0boot0` writes can overwrite the record if they extend into
the reserved area.
This is a boot-time MAC store, not an EFI runtime variable backend.

Hardware checks on the V2 minor revision covered both RTL8211F IDs,
GMAC1 at 1000 Mbps
full duplex, DHCP, ten 1472-byte pings to the gateway without loss,
MAC reuse after reset and firmware update, and 16,384 internally
looped-back 1514-byte frames (about 45 MiB/s).
The other jack still needs a cable test. PXE is included; downloading
and booting a network image have not been tested.

The RAM CBFS is 1.5 MiB and the two-stage BootROM download fits in the
2 MiB OCRAM window. The normal boot0 image is 2080 KiB. Validation of
a packaged image must identify its exact DTB and firmware inputs.

## HDMI

The A210 DC8200 output 1 feeds DW-HDMI. Firmware exposes a GOP using the
sink's preferred EDID timing, up to 1920x1080, while retaining the serial
console. The common DC8200 driver and graphics-console helper are also
used by TH1520; A210 resources remain in its platform library.

Coreboot must enable carrier 5 V and 3.3 V, power VO and configure its
IOPMP entries. The carrier's 1.8 V DDC supply depends on 3.3 V power-good.
The DTB must include the DC8200, HDMI, VO clock and connector graph, plus
GPIO2_25/30/31 in HDMI mode. The board DTS keeps the carrier enable GPIOs
claimed across OS handoff.

DPU1 PLL is already 1188 MHz after BootROM initialization. Firmware
changes only the channel divider and HDMI input mux, without retuning
that PLL. The capture card selected 1920x1080 at 148.5 MHz. Both UEFI
Shell and F2 setup were captured after a complete boot0 restart, with
serial output still available. The console retains its 80x25 layout.

The USB capture card was tested while actively streaming with ffmpeg.
A captured 1280x720 frame is the capture device's output format; it does
not change the 1920x1080 HDMI timing selected by GOP. Native Linux
DC8200/HDMI takeover has not been validated by these firmware tests.

References:

- Vendor kernel: `drivers/gpu/drm/verisilicon/` and the A210 board DTS.
- [V2 minor-revision schematic][a210-v3-schematic], labeled V3 by
  the vendor, power and HDMI pages.

[a210-v3-schematic]: https://developer.zhcomputing.com/zh/docs/A210/Hardware-Platform/Baseboard-Board-diagram/Baseboard_Board_V3/

## Device tree layout

Build `zhihe/a210-melonpi-v2.dtb` from the target kernel. The board
includes
`a210-som.dtsi`, which includes `a210.dtsi`:

- `a210.dtsi` describes the SoC controllers and their internal connections.
- `a210-som.dtsi` describes the SODIMM module: memory, eMMC, Ethernet PHYs,
  supplies, and clock and voltage policy.
- `a210-melonpi-common.dtsi` describes shared carrier resources.
- `a210-melonpi-v1.dts` and `a210-melonpi-v2.dts` describe the two
  carriers, including their identity, connectors and power controls.

The memory size in the source is a placeholder. Coreboot replaces it
with the capacity measured during DDR initialization. EDK2 uses the
resulting DTB for device discovery and passes it to the operating system.
