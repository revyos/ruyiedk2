# DesignWare USB2 host

Based on TianoCore edk2-platforms `DwUsbHostDxe`, revision
`9c2cdde2d73d6bf0261e57b79bfb68cd98fccc1f`, under
`Platform/RaspberryPi/Drivers/DwUsbHostDxe`.

The host-controller protocol and transfer engine are retained.
`Dwc2HostPlatformLib` replaces the Raspberry Pi mailbox and fixed
register address. DMA uses the standard `DmaLib`; barriers use
`MemoryFence`. This instance supports a UTMI PHY and one controller.
Platform initialization must select a host-only controller separately
from any download controller.

Reset handles both the self-clearing request and the 4.20a reset-done
handshake, including at ExitBootServices. Root-port full/low-speed
devices do not use transaction-translator splits. The controller has
32-bit DMA addresses; the platform must constrain its DmaLib instance.
An unhalted channel blocks further transfers until a successful reset.
