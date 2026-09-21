# RAM recovery profile

`make edk2-recovery` builds `build/meles-recovery/TH1520.fd`.
This is a minimal Meles EDK2 firmware volume for an external RAM loader.
It keeps serial console, eMMC and USB device Fastboot, and starts the
embedded Fastboot application through BDS without a boot menu timeout.
The regular Meles and LPi4A profiles keep their normal boot policy.

The FD is 4 MiB at physical address `0x20000000`. It expects S-mode
entry with a valid DTB in a1 and a working OpenSBI runtime. Temporary
memory occupies `0x82000000` through `0x84000000`. A loader may compress
the FD to fit the BootROM download limit, then expand it into DDR.
The sibling coreboot tree has `util/thead/build-recovery.py` for this
purpose; neither a U-Boot runtime nor its Fastboot implementation is
part of this recovery path.

## Fastboot targets

Only the recovery profile exposes these raw targets:

| Target | Device range | Size |
| --- | --- | --- |
| `spi` | SPI NOR, starting at offset zero | 8 MiB |
| `emmcboot0` | eMMC hardware boot partition 1 | Reported by eMMC |
| `emmcboot1` | eMMC hardware boot partition 2 | Reported by eMMC |

For the tested Meles eMMC, each boot partition is 4 MiB. Query capacity
before selecting an image:

```sh
fastboot getvar product
fastboot getvar partition-size:spi
fastboot getvar partition-size:emmcboot0
fastboot getvar partition-size:emmcboot1
```

Once recovery is running, write a complete image for the selected
medium, for example:

```sh
fastboot -S 0 flash spi meles-spi.bin
# Or select one eMMC boot partition:
fastboot -S 0 flash emmcboot0 meles-emmc.bin
fastboot -S 0 flash emmcboot1 meles-emmc.bin
```

Raw recovery images are bounded by the target capacity and must not be
Android sparse containers. They do not require the normal SPL/FIT
`loader` format. Writes are read back and compared by the existing
writers. eMMC operations restore access to the user area and preserve
boot-enable/boot-ack configuration; writing boot1 does not select it as
the active BootROM boot partition. There is no raw user-area target.

SPI erases whole 4 KiB sectors covering the image. eMMC pads its final
512-byte block with `0xff`. Bytes beyond that erase/write span remain
unchanged. The existing `flash:loader` validation is also retained.

## Validation

Meles 16 GiB hardware completed the BootROM RAM -> coreboot DDR ->
OpenSBI -> EDK2 path and enumerated automatically as `meles-recovery`.
The image is 1 MiB, below the 1152 KiB BootROM download limit. OpenSBI
uses its original relocatable dynamic binary; boot harts 1 and 2 were
observed without a hart-0 policy.

The recovery service reported 8 MiB SPI and 4 MiB per eMMC boot area.
It rejected a sparse input before writing. A 1 MiB image was written to
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

Host tests cover target names, bounds, unavailable eMMC and sparse
rejection. Both normal board profiles and the recovery profile build.
