# RAM recovery profile

`make edk2-recovery` builds `build/meles-recovery/TH1520.fd`.
`make edk2-lpi4a-recovery` builds
`build/lpi4a-recovery/TH1520.fd`. Each board has its own product
identity, board support library and device tree. These are minimal
EDK2 firmware volumes for an external RAM loader.
It keeps serial console, eMMC and USB device Fastboot, and starts the
embedded Fastboot application through BDS without a boot menu timeout.
The regular Meles and LPi4A profiles keep their normal boot policy.

The FD is 4 MiB at physical address `0x20000000`. It expects S-mode
entry with a valid DTB in a1 and a working OpenSBI runtime. Temporary
memory occupies `0x82000000` through `0x84000000`. A loader may compress
the FD to fit the BootROM download limit, then expand it into DDR.
The sibling coreboot tree has `util/thead/build-recovery.py` for this
purpose. Pass `--board meles` or `--board lpi4a` and the matching FD and
DTB. The resulting RAM images are separate and must not be exchanged
between boards. Neither a U-Boot runtime nor its Fastboot implementation
is part of this recovery path.

## Fastboot media access

RAM recovery and normal EDK2 both expose `spi`, `emmc0`, `emmc0boot0`
and `emmc0boot1` for reading and writing. Commands address the medium
by name and write opaque bytes without image-format validation.
See [Fastboot media access](fastboot-storage.md) for capacities, reads,
offsets, physical-block handling and the host helper.

## Validation

Meles 16 GiB hardware completed the BootROM RAM -> coreboot DDR ->
OpenSBI -> EDK2 path and enumerated automatically as `meles-recovery`.
The image is 1 MiB, below the 1152 KiB BootROM download limit. OpenSBI
uses its original relocatable dynamic binary; boot harts 1 and 2 were
observed without a hart-0 policy.

LPi4A 8 GiB hardware also completed BootROM RAM download, DDR training,
address-map adjustment, AON voltage verification, OpenSBI and EDK2
Fastboot. It enumerated as `lpi4a-recovery`. Read-only capacity queries
for the earlier `emmcboot0`, `emmcboot1` and `spi` names succeeded. Use the coreboot
normal eMMC profile to build the LPi4A boot0 image. Meles normal
firmware uses the coreboot SPI profile. Both paths hand off through
OpenSBI to the corresponding normal EDK2 volume.

The Meles recovery service reported 8 MiB SPI and 4 MiB per eMMC boot area.
The earlier recovery version rejected sparse inputs; the current raw
interface writes those bytes verbatim. A 1 MiB image was written to
SPI, boot0 and boot1, with each writer's readback comparison passing.
The eMMC user area was not written and boot selection was preserved.

The SPI image restarted and returned to Fastboot. A temporary SPI boot
stage also loaded the rest of the image from eMMC boot0 and returned to
Fastboot, after which the SPI-source boot stage was restored. This
validates coreboot's eMMC CBFS path, but not BootROM's direct eMMC
startup or boot1 source selection.

The DEBUG run caught dependencies required by the minimal profile:
publish the DTB HOB before board reset delays, retain the mandatory
Capsule architectural protocol, and disable the optional driver-health
form check when no forms browser is included. The RELEASE SPI payload
also completed hardware startup.

Current host tests cover media names, 64-bit byte ranges, partial-block
preservation, opaque contents and I/O failures. The earlier board receipts
above describe the preceding recovery interface; they do not validate
the current interface. Record new build and board results separately.
