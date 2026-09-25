# TH1520 EDK2

EDK2 platform support for Milk-V Meles and Sipeed Lichee Pi 4A.

- `platforms/Th1520Pkg/`: shared drivers, libraries, Fastboot and boot UI.
- `platforms/MelesPkg/`: Meles board configuration.
- `platforms/Lpi4aPkg/`: Lichee Pi 4A board configuration.
- `tests/`: host tests and HSM, storage and network EFI test applications.
- `tools/build.sh` and `Makefile`: build and test entry points.

Boot stages, OpenSBI, Linux device trees, DDR/AON firmware and complete
image packaging are maintained in the parent TH1520 workspace.

## Build

The build uses the sibling `../edk2` checkout with its submodules
initialized. The upstream baseline is
`6abc1c06585ffd66d23a74811a930d8d0f88cef7`, without local patches.
The platform IPv4 settings page uses standard HII and IPv4 Config2
protocols. The parent workspace pins the EDK2 revision; the build script
uses that checkout directly and does not apply patches automatically.
See [IPv4 configuration](docs/network-configuration.md).

Install a RISC-V GCC cross toolchain, host GCC/G++, make, Python 3,
NASM, and UUID and OpenSSL development libraries. Host tests also need
libfdt and zlib development libraries. DTB builds require `dtc`.

```sh
make edk2 EDK2_BOARD=meles
make edk2 EDK2_BOARD=lpi4a
# Minimal serial Fastboot recovery profile:
make edk2-recovery
make edk2-lpi4a-recovery
# Use an EDK2 checkout elsewhere:
make edk2 EDK2_SOURCE=/path/to/edk2 EDK2_BOARD=meles
```

Defaults are `EDK2_TARGET=RELEASE`, `JOBS=8` and
`CROSS_COMPILE=riscv64-linux-gnu-`. All build output stays in `build/`.
The normal firmware volumes are `build/meles/TH1520.fd` and
`build/lpi4a/TH1520.fd`; recovery volumes are
`build/meles-recovery/TH1520.fd` and
`build/lpi4a-recovery/TH1520.fd`. These volumes must be packaged with
boot firmware before they can be flashed to SPI or eMMC boot0.

Build the matching device tree with:

```sh
make edk2-dtb EDK2_BOARD=meles
make edk2-dtb EDK2_BOARD=lpi4a
```

`KERNEL_SOURCE` defaults to `../linux-mainline`. Each DTB is written to
`build/<board>/firmware.dtb`. Pass it with the firmware volume to the
image packager. See [variable storage](docs/variables.md) for the Meles
QSPI resources required by Runtime Services.

## Tests

```sh
make check
make edk2-hsm-test EDK2_BOARD=lpi4a
```

`make check` covers display, DMA, reset, MAC policy, storage and IPv4
forms. The HSM test is built as
`build/Build/Lpi4a/RELEASE_GCC/RISCV64/HsmTest.efi`.
Run it from the target's UEFI Shell; it is not in the default image.

Artifact checks use existing board DTBs and SPL/FIT images to exercise
image validation, device-tree parsing and USB power control:

```sh
make check-artifacts EDK2_BOARD=meles
make check-artifacts EDK2_BOARD=lpi4a
# Use artifacts outside the default ../build directory:
make check-artifacts FIRMWARE_BUILD=/path/to/th1520/build
```

These targets consume existing artifacts without building or flashing
external firmware. For EFI I/O benchmarks, see
[I/O performance](docs/io-performance.md).

## Documentation

- [Device tree](docs/device-tree.md): resource parsing and board wiring.
- [Display](docs/display.md): HDMI modes, clocks and OS handoff.
- [Recovery](docs/recovery.md): RAM profile and Fastboot targets.
- [Variables](docs/variables.md): persistent variables and BIOS MAC setup.
- [IPv4 configuration](docs/network-configuration.md): form save behavior.

## Source and licensing

The platform originated in the TH1520 workspace at `74380f02774d`.
Files retain their SPDX identifiers and copyright notices. The
[logo documentation](platforms/Th1520Pkg/Drivers/LogoDxe/README.md)
records the openRuyi artwork source and attribution.
Hardware validation and its limits are recorded in the feature guides.
