# TH1520 EDK2 HDMI bring-up

The display path is CPU rendering into a linear framebuffer, DC8200
output 1, then the Synopsys DesignWare HDMI transmitter and its PHY.
It does not require the GPU, a GPU firmware blob, or a Linux DRM stack.
DSI and panel initialization are outside this first implementation.

## Sources

The primary reference is the open DC8200 driver in the local
`~/code/kernel/linux/revyos-linux` repository, branch
`revyos/7.3.y` (remote-tracking `revyos/revyos/7.3.y`), commit
`59fb4a4d133d08cf46a28be07be58d8c7a82b870`:

- `drivers/gpu/drm/verisilicon/vs_{crtc,bridge,primary_plane}.c`
- The corresponding DC8200 register headers.
- `drivers/gpu/drm/bridge/th1520-dw-hdmi.c`
- `drivers/gpu/drm/bridge/synopsys/dw-hdmi.{c,h}`
- `drivers/clk/thead/clk-th1520-ap.c`
- `drivers/reset/reset-th1520.c`

The TH1520 vendor reference is the parent repository’s `linux/`
submodule, remote
`https://github.com/revyos/th1520-linux-kernel.git`, commit
`a092d55649279e1c9bcda2769b8f6b4370fa2c94` on `th1520-lts`.
Its `drivers/gpu/drm/verisilicon/` and DW-HDMI core are useful for
checking cold-start initialization and differences in shadow handling.
The same vendor tree is available as `th1520/th1520-lts` in the
local RevyOS checkout. It is a TH1520 reference; A210 platform sequences
are not used in this port.

The port preserves GPL licensing and the attribution of copied register
definitions and adapted hardware sequences. EDK2 console integration
uses the existing GraphicsConsoleDxe and FrameBufferBltLib.

## Implementation

`Th1520Pkg/Drivers/DisplayDxe` is shared by TH1520 boards. The driver
requires enabled `thead,th1520-dc8200` and `thead,th1520-dw-hdmi` nodes
at the expected SoC addresses. LPi4A already describes that output graph.
It does not enable disabled display nodes or touch GPU power or reset.
Meles enables the same output through its board DT, including the
Type-A connector and reciprocal HDMI graph endpoints.

- GOP exposes EDID-supported 1920x1080, 1280x720, 800x600 and 640x480.
  Established and standard EDID timings, detailed timings and the first
  CTA extension are parsed, with checksum and bounds validation.
- Preserve the existing DPU1 PLL configuration. Its documented reset
  values are CFG0=`0x01206301`, CFG1=`0x03000000`, giving 1188 MHz.
  Only enable the PLL if needed and select a one-based pixel divider,
  rounding up like the Linux divider driver. Dividers 8, 16, 30 and 48
  produce 148.5, 74.25, 39.6 and 24.75 MHz respectively. The last two
  approximate the nominal SVGA/VGA clocks; firmware does not retune the
  PLL to make those clocks exact. VIDEO_PLL also remains untouched.
  No assigned-clock-rate override is added to the Linux device tree.
- Standard EDK2 PCDs request 800x600 for boot and 640x480 for Setup,
  both with 80x25 text. A sink lacking a requested mode uses the existing
  EDK2 fallback path. No custom console scaling or GOP wrapper is used.
- The scanout format is linear XRGB8888, exposed by GOP as BGRX bytes.
  Framebuffer pages are reserved below 4 GiB for the DC DMA address.
- GOP Blt operations flush changed framebuffer ranges. A boot-services
  timer also flushes direct GOP framebuffer writes every 100 ms because
  firmware uses bare addressing with cacheable DDR.
- ExitBootServices performs a final clean/invalidate and leaves scanout
  and its reserved pages in place. The OS must provide suitable framebuffer
  mappings and its own display/cache management after that point.
- The graphics device joins ConOut and ErrOut alongside the serial
  terminal. EDK2 Console Splitter mirrors menu, Shell and error output.
  SPL/OpenSBI and direct serial debug messages remain on UART.
  F2, F9 and F12 retain their existing meanings.
- No HPD, invalid EDID, unsupported modes or bounded hardware timeouts
  leave the serial console available. There is no forced output mode in
  the normal driver, and no HDMI hotplug reprobe after DXE initialization.

The implementation does not include HDMI audio, HDCP, 4K/SCDC,
arbitrary EDID modes, or a DSI panel driver. OS console policy remains
under the control of the bootloader and kernel.

## Checks and capture

Build commands below run in this repository. Hardware capture records
and full SPI/boot0 image paths refer to the parent `th1520` repository.

```sh
make edk2-display-check EDK2_BOARD=lpi4a
make edk2-lpi4a
make check-artifacts EDK2_BOARD=lpi4a
```

The display checks exercise EDID truncation/corruption, established and
standard low modes, sync polarities, PLL configuration preservation,
PHY NACKs, bounded timeouts and scanout shutdown on failure. They are software checks,
not evidence that HDMI has been electrically validated.

On the connected development host, `ffmpeg` and `v4l-utils` provide capture.
The current user can open the following device without extra udev rules:

```sh
capture=/dev/v4l/by-id/usb-MACROSILICON_USB3.0_UHD_20210623-video-index0
v4l2-ctl -d "$capture" --all --list-formats-ext
ffmpeg -f v4l2 -input_format yuyv422 -video_size 1280x720 \
  -framerate 30 -i "$capture" -vf fps=1 -q:v 3 \
  -update 1 -atomic_writing 1 -y hdmi-live.jpg
```

Use the capture card's HDMI input and start OBS or continuous capture
**before booting the board**. This MACROSILICON capture device asserts
HPD and serves its EDID while capture is active. In Linux, starting
capture changes the connector to `connected` with a 256-byte EDID;
stopping capture changes it back to `disconnected` with an empty EDID.
Single-frame capture is therefore unsuitable for keeping this sink
connected during firmware initialization. Wait for a stable frame after
menu mode changes before saving a snapshot. Stop capture with Ctrl-C
when handing the device back to OBS.

## Validated behavior

The LPi4A capture sink advertises both supported modes. Linux
`7.0.0-31-ruyi` selects 1920x1080 at 60 Hz, a 148.5 MHz pixel clock,
linear XRGB8888 and a 7680-byte stride. Its PHY status is `0xf3`.
The corresponding working DC8200 registers match the firmware's
configuration. RevyOS 7.3.y remains the hardware programming reference.

The native 1080p path and the low-resolution boot/setup paths have been
exercised on LPi4A. The following are verified:

- Boot0 flash and per-chunk readback verification.
- openRuyi logo and the standard BDS ten-second progress bar on HDMI.
- F2/F9/F12 accepted during that progress window.
- F12 Boot Menu simultaneously visible on HDMI and UART.
- VGA Firmware Setup fills the active HDMI frame with board model,
  1.84 GHz and 8192 MB RAM; serial remains 80x25.
- UEFI Shell text simultaneously visible on HDMI and UART.
- Both ConOut and ErrOut contain the serial path and static GOP path.
- F9 USB Fastboot responds with product `lpi4a-edk2`.
- Launching the installed `fs0:\EFI\BOOT\BOOTRISCV64.EFI` boots Linux.
  Its early simpledrm framebuffer is replaced by native verisilicon-dc
  around 10.7 seconds; HDMI reaches the Ubuntu tty1 login screen and
  the UART root console remains usable. This validates display takeover,
  not uninterrupted output throughout the userspace boot process.
- Linux handoff retains the 1188 MHz DPU1 PLL and selects a 148.5 MHz
  pixel clock for 1080p without any assigned-clock-rate override.
- LPi4A image policy checks and Meles firmware compilation.

A GOP handle alone is insufficient to validate the console. The initial
static GOP path included an ACPI ADR suffix without a parent controller
handle. BDS truncated that suffix, failed to find the parent, and removed
the graphics path from ConOut. The framebuffer consequently stayed black
while serial output worked. The driver now exposes its vendor controller
path directly, allowing standard BDS and GraphicsConsole to connect it.
No common EDK2 console driver changes are required.

The current verified LPi4A boot0 image is 1,420,852 bytes. Its SHA-256
is recorded in `build/edk2-lpi4a/tested-boot0.sha256`:

```text
6239c703a613e8b29f9cf652ea7c14754081b0c758a390e2e9f48a1a03a22098
```

Historical hardware artifacts remain in the original development checkout
under `build/edk2-lpi4a/`: the captured EDID,
Linux register/state samples, and HDMI Boot Menu, Setup and Shell images.
A physical 720p mode switch has not been tested.

Meles has also been flashed and tested without JTAG through its USB-C
Fastboot SPI `loader`. On the 16 GiB dual-rank board, the openRuyi
progress screen, progress-window F2/F9/F12, VGA Setup, SVGA Shell, and
simultaneous HDMI/UART output are verified. Setup reports Milk-V Meles,
1.84 GHz and 16384 MB RAM. PLL CFG0/CFG1 remain `0x01206301` and
`0x03000000` after mode changes. The packaged DTB passes display
schemas and retains the GL3523 peer-hub/connector graph. Native Linux
HDMI takeover on Meles has not yet been tested.

Meles SPI image: `build/edk2/uboot/meles-edk2-spi.bin`, 1,417,452 bytes.
SHA-256: `1a974bbf1a8df752aa67ea9937afb211190868049d7cfa1039c56c4d776b4576`.
The original manifest and capture evidence remain under its `build/edk2/`.

## Startup presentation

The original openRuyi SVG and its BMP conversion are stored in
`Th1520Pkg/Drivers/LogoDxe`. A small platform logo provider uses the
existing BMP decoder; `BootLogoLib` handles presentation and progress.
The hotkey hint is the progress-bar title. The native BDS wait owns the
single ten-second timeout; there is no separate manual key-wait loop.
Serial receives the key hint/countdown, and factory-MAC warnings remain
visible. The normal unattended timeout was verified to boot Linux.

Firmware does not force Linux clock frequencies at handoff. DC8200 is
reinitialized by its native driver. The display PLL reference, feedback,
postdividers and fractional-mode bits are preserved across GOP modes.
