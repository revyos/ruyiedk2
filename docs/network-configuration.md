# IPv4 configuration

The BIOS network Configure page shows the selected DHCP policy or
manual address settings even before the interface has an active address.
DHCP address acquisition still runs asynchronously.

Editing fields changes only browser data. F10 Save and Save Changes and
Exit submit the form through RouteConfig. The IPv4 driver applies the
fields, writes the complete configuration once, and returns the storage
result. A failed write is reported to the browser. Discarding edits
keeps the previously submitted configuration.

The Meles SPI variable store completes that write before returning.
Identical variable contents do not append another flash journal record.
LPi4A still has volatile general UEFI variables; its eMMC MAC storage is
separate and does not persist IPv4 settings across power cycles.

## Source and tests

These fixes belong to the common EDK2 NetworkPkg, not the TH1520 GMAC
or SPI drivers. The parent repository pins the local EDK2 commits:

- `b3b698bc6ff9`: show configured values before address acquisition.
- `de036276f627`: persist each HII submission once and return write errors.

`make edk2-ip4-form-check` exercises the actual IPv4 form and variable
serialization code. It covers DHCP without a configured interface,
manual addresses without a gateway, edit and save callbacks, one write
per submission, stable serialization, DNS removal, invalid input and
storage failures.
The test is also part of `make check`.

Meles hardware validation on 2026-09-23:

- F10 saved `192.0.2.8/24`, gateway `192.0.2.1` and DNS `192.0.2.53`.
  All four values appeared when reopening the page and after reset.
- Save Changes and Exit restored DHCP. Reopening immediately showed
  both Configure and Enable DHCP selected.
- Toggling DHCP without saving, then discarding the edit, kept DHCP.
- Linux booted normally; MAC, boot order and existing variables remained
  intact. Ten 1472-byte pings passed without loss.

The documentation-range static settings were temporary test values.
DHCP was restored after testing. Meles, LPi4A and recovery builds pass;
only Meles was tested on hardware.
