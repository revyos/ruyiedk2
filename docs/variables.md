# Persistent variables and MAC settings

Meles stores UEFI variables in SPI NOR and exposes the standard UEFI
Runtime Services to Linux. LPi4A uses volatile general UEFI variables;
its eMMC boot0 area stores only MAC settings used during firmware boot.

## Storage layout

| Board | Firmware limit | Reserved region |
| --- | --- | --- |
| Meles, 8 MiB SPI | `0x7c0000` bytes | Last 256 KiB: two variable banks |
| LPi4A, 4 MiB boot0 | `0x3fe000` bytes | Last 8 KiB: two MAC records |

Each SPI bank occupies 128 KiB and holds up to 64 KiB of serialized
variable data. The writer stores and verifies the data before committing
the header with its generation number and CRC. The other valid bank
remains available until the update succeeds.

boot0 uses two 512-byte records spaced 4 KiB apart, with version,
generation and CRC fields. Version 2 stores both the default random MAC
and BIOS settings and can read version 1 records. Updates use the other
copy so interrupted or invalid writes leave the previous copy usable.
The original random address is retained; unchanged settings are not
written again.

Fastboot `spi`, `emmcboot0` and normal firmware updates exclude these
reserved regions. boot1 is not used for variable storage. External
programmers and older whole-device erase tools may overwrite them.

## MAC selection

Addresses are selected in this order:

1. A valid device-tree address.
2. A valid BIOS override.
3. A valid factory eFuse address.
4. The saved default random address.
5. A newly generated random address, saved for later boots.

Every source is validated. Addresses must contain six bytes and be
nonzero, unicast and nonbroadcast. Invalid candidates are skipped.
Saved random base addresses must also be locally administered and have
an even final byte. boot0 records must pass version and CRC checks.

The DT lookup tries `mac-address`, then `local-mac-address`, before
falling back to BIOS settings and eFuse. Explicit valid assignments
need not be adjacent. Candidates for both ports are collected before
either is applied, so probe order does not affect selection.

GMAC1 uses the address adjacent to GMAC0's final address only when it
needs a fallback. Carry must not produce a multicast address. The
selected addresses are written to both `mac-address` and
`local-mac-address` in the DT passed to the operating system.

MAC persistence is managed during firmware boot. SPI variables
`FallbackMac` and `MacSettings` have no Runtime Access attribute.
Runtime SetVariable rejects creation, modification and deletion of both
reserved names, including when they do not yet exist. EndOfDxe locks
the default fallback record; BIOS settings remain editable through the
boot-time setup interface.

The boot0 storage protocol is available only during Boot Services and
uses the same fallback-record lock. Linux receives the selected MACs
through the DT without a writable persistent-MAC interface.

## BIOS settings

Set the delay under `Boot Maintenance Manager > Auto Boot Time-out`
and save with F10. Normal firmware uses the standard `Timeout` variable,
with a ten-second default when it is absent. RAM recovery enters
Fastboot directly.

Open `F2 > Device Manager > Ethernet MAC Addresses`:

- Enable `Use BIOS address` and enter six colon-separated hex octets.
- `Generate new random addresses` previews new addresses and enables
  BIOS overrides. Dual-port addresses are adjacent. If GMAC0 has a DT
  address, the generated GMAC1 address is adjacent to it.
- `Restore automatic selection` clears BIOS overrides and retains the
  original saved random address.
- `Save and restart` persists the settings and reboots. F10 also saves;
  restart afterward to apply the changes.

Only ports present in the DT are shown. Valid DT assignments take
priority and disable the corresponding editors. Zero, multicast,
broadcast and malformed input produces an error and is not saved.
Manual entry and generation update the same BIOS settings; neither
writes eFuse.

## Dependencies and runtime access

VariableRuntimeDxe provides standard variable validation.
Th1520NvStoreDxe handles persistence, virtual-address conversion and
reserved storage. Linux accesses variables through standard UEFI
Runtime Services; OpenSBI needs no storage extension.

Meles requires the matching `Firmware.dts`. It describes the QSPI0
controller, clocks and native chip-select pins, with the NOR child
marked `reserved` so SPI NOR/MTD does not bind it. Existing Linux
DesignWare SPI, TH1520 clock and pinctrl drivers keep the controller
resources available.

Runtime SPI access uses native chip select and controller registers.
It does not change shared GPIOs, pinctrl or clock gates. An unavailable
controller returns an error rather than reporting a successful save.
On LPi4A, GMAC waits for the eMMC storage protocol before selecting
addresses. Firmware does not access MMC after the Linux handoff.

Build the matching DTB:

```sh
make edk2-dtb EDK2_BOARD=meles
make edk2-dtb EDK2_BOARD=lpi4a
```

Board descriptions come from `KERNEL_SOURCE`, which defaults to
`../linux-mainline`. Supply `build/<board>/firmware.dtb` together with
the FD to the image packager. Meles variable persistence requires the
QSPI resource description as well as the firmware driver.

### C910 runtime MMIO

The RISC-V Linux EFI mappings used for validation map MMIO as normal
memory. C910 XTheadMae uses MAEE page attributes, which differ from
standard Svpbmt encodings. Firmware reserves page tables containing
one register-access code page and the QSPI and AP register pages.
Device mappings use T-Head SO/SH attributes without the normal-memory
C/B bits.

Each runtime access masks local interrupts, switches page tables,
performs one 32-bit register access, and restores `satp` and interrupt
state. The switch uses registers only, without touching the caller's
stack or Linux page tables or coordinating other harts. Address
conversion failures prevent persistent writes. This path is internal
to TH1520 firmware.

## Validation

Meles hardware checks covered standard efivarfs writes and persistence
across restart and firmware updates. The saved default MAC remained
`C2:F5:52:1A:87:16`. Invalid broadcast and multicast DT candidates were
ignored, and both runtime DT MAC properties contained the selected
address. Both private MAC variables were hidden from Linux, and runtime
writes returned write-protected status.

BIOS checks covered manual addresses, regeneration, restart activation
and restoration of the original random address. LPi4A passed builds and
host tests for boot0 migration and interrupted updates; these storage
checks did not include LPi4A hardware or HDMI hotplug.

Meles boot-option checks on 2026-09-23 used the existing ESP file
`\EFI\ubuntu\grubriscv64.efi` without changing the loader:

- An `efibootmgr` entry placed first in `BootOrder` survived restart.
  `BootCurrent` confirmed that the new entry was selected.
- `BootNext` selected the requested entry once, then was cleared while
  preserving the default order.
- A BIOS `Add Boot Option` entry with a full device path survived
  restart, appeared in F12 and booted successfully, as confirmed by
  `BootCurrent`.
- Removing test entries and restoring the order returned the board to
  its original default, without leftover or duplicate firmware entries.
