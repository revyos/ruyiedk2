# IPv4 configuration

Open Device Manager, select the network interface by its MAC address,
and choose **IPv4 Settings**. Each IPv4 Config2 interface gets its own
page. The original EDK2 network forms remain available and unmodified.
Use IPv4 Settings to view the saved policy before acquiring an address.

The page supports DHCP or one static address, one optional gateway and
up to 16 DNS servers. Separate DNS addresses with spaces. An empty
gateway or DNS field removes that setting. Existing configurations
with more addresses than the page can represent are rejected rather
than silently truncated.

Editing fields changes only browser data. F10 and Save Changes and Exit
submit through RouteConfig, which validates all fields before calling
EFI_IP4_CONFIG2_PROTOCOL.SetData. Unchanged fields are skipped. Changed
fields can cause several persistent writes during one submission.
Discarding edits does not call SetData.

The page reads policy and manual settings through GetData, independent
of the active interface address. DHCP acquisition remains asynchronous.
If SetData requires asynchronous address validation, submission waits
for the standard notification and checks the result, with a five-second
timeout. A timeout or protocol error is reported instead of success.

Submission is not atomic: a failure after applying one field can leave
that field changed. Reopen the page to inspect the current configuration.
The page relies on the configuration protocol for persistence. The pinned
upstream Ip4Dxe does not propagate every variable-write error through
SetData; the page cannot independently guarantee storage-error reporting.
It does not parse or write Ip4Dxe's private variable format.

Meles stores general UEFI variables in SPI NOR. Its variable store writes
synchronously and skips identical contents. LPi4A general UEFI variables
remain volatile; its separate eMMC MAC storage does not persist IPv4
settings across power cycles.

## Implementation and tests

`platforms/Th1520Pkg/Drivers/Ip4ConfigDxe` provides the HII frontend.
It binds to IPv4 Config2 instances and releases its form when disconnected.
It has no GMAC, SPI, device-tree or private NetworkPkg dependencies.
No modifications to the upstream EDK2 checkout are required.

`make edk2-ip4-form-check` tests the platform form and configuration code
against mock standard protocols. It covers independent interfaces, saved
values without a live address, deferred submission, unchanged settings,
partial updates, gateway and DNS removal, invalid input, asynchronous
completion, timeout and protocol failures. It is also in `make check`.
These host tests do not prove physical persistence or browser behavior;
those require firmware tests on the board.

## Hardware validation

Meles SPI firmware was tested over the serial BIOS console on 2026-09-25:

- F10 saved `192.0.2.8/24`, gateway `192.0.2.1`, and DNS servers
  `192.0.2.53` and `198.51.100.53`. All fields appeared on reopening and
  after a firmware reset.
- Discarding an unsaved DHCP selection retained the static settings.
- Empty gateway and DNS fields were saved and remained empty on reopening.
- Invalid input was rejected without replacing the saved configuration.
- Save Changes and Exit restored DHCP; reopening and another firmware
  reset both showed it selected.

The documentation-range addresses were temporary test settings. DHCP was
restored after testing. Meles, LPi4A and RAM recovery builds pass; the
physical persistence tests cover Meles only.

Ubuntu `7.0.0-31-ruyi` booted normally after the tests. The MAC address
and 11 existing boot-variable digests were unchanged. Ten pings with a
1472-byte payload completed without loss.
