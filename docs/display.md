# TH1520 EDK2 HDMI

`Th1520Pkg/Drivers/DisplayDxe` renders into a linear framebuffer and
uses DC8200 output 1 with the DesignWare HDMI transmitter and PHY.
It requires enabled `thead,th1520-dc8200` and `thead,th1520-dw-hdmi`
nodes at the SoC addresses. Meles and LPi4A share the driver.
GPU initialization and DSI panels are not required or implemented.

## Modes and clocks

GOP mode 0 is the first supported choice in this order:

1. The base EDID's preferred detailed timing.
2. A native mode in the first CTA extension.
3. Another supported mode advertised by the sink.

Detailed timings retain their porches, sync widths and independent H/V
polarities. The driver supports progressive separate-sync modes up to
1920x1080 and 148.5 MHz, without borders. It also recognizes established
and standard 800x600/640x480 modes and CTA VICs 1, 4 and 16. Invalid
checksums, malformed blocks and unsupported modes are rejected.
Without a usable mode, the serial console remains available.

The DPU1 PLL configuration is preserved. Its reset configuration,
CFG0=`0x01206301` and CFG1=`0x03000000`, produces 1188 MHz. The driver
only enables the PLL if needed and rounds the pixel divider up, as the
Linux divider driver does. Dividers 8, 16, 30 and 48 produce 148.5,
74.25, 39.6 and 24.75 MHz. The last two approximate the nominal SVGA
and VGA clocks. Other detailed timings may also use an approximate
clock; the firmware does not retune the PLL to match them exactly.
VIDEO_PLL is unchanged, and no DT clock-rate override is added.

HDMI audio, HDCP, 4K/SCDC, interlaced or composite-sync timings,
DisplayID and further EDID extensions are not supported.

## Console and handoff

BootManager copies GOP's selected resolution into the boot and Setup
video PCDs before connecting GraphicsConsole. The countdown and menus
keep that mode. Text dimensions remain 80x25, with the standard Console
Splitter mirroring output to HDMI and serial. The GOP uses a vendor
controller device path directly so BDS can connect its console.

LogoDxe supplies the openRuyi bitmap to BootLogoLib. The standard BDS
countdown provides the ten-second timeout and F2/F9/F12 hotkeys.
Enter skips the remaining countdown and continues normal boot without
changing the saved timeout or boot order. It uses BDS's continue-key
registration for both serial and USB keyboards. The seconds field has
fixed width so redrawing the title does not leave old characters behind.

Scanout uses linear XRGB8888, exposed by GOP as BGRX bytes, with a
128-byte-aligned stride and reserved framebuffer pages below 4 GiB.
Blt flushes changed ranges; a boot-services timer also flushes direct
framebuffer writes every 100 ms because DDR is cacheable. HDMI attach
is polled every 500 ms. Reconnecting a different mode catalogue
reconnects GraphicsConsole so its cached modes are refreshed.

ExitBootServices stops polling, performs a final cache clean/invalidate,
and leaves scanout and its reserved pages intact. The OS then owns
modesetting, framebuffer mappings and cache management.

## Build and checks

Run from this repository:

```sh
make edk2-display-check edk2-hotplug-check
make edk2 EDK2_BOARD=meles
make edk2 EDK2_BOARD=lpi4a
```

Host tests cover EDID preference and fallback, malformed descriptors,
independent sync polarities, stride alignment, PLL preservation, PHY
failures and hotplug. They do not replace hardware validation.

On 2026-09-25, Meles SPI write/readback and HDMI capture confirmed the
EDID-preferred 1920x1080p60 mode at 148.5 MHz. The countdown and F2
Setup were visible; a subsequent Shell query still reported GOP mode 0
at 1920x1080. Serial remained usable. Both board builds passed.

With that firmware, five warm reboots of unmodified Ubuntu
`7.0.0-31-ruyi` reached clear HDMI login screens. Both DPU clocks kept
`CLK_SET_RATE_UNGATE`; no temporary driver, reload or MMIO repair was
used. Four boots had one DC probe and one had two. This sample did not
reproduce the earlier corruption. It does not establish its root cause
or cover power cycling and normal monitors. LPi4A's current EDID policy
has been compiled but has not received the same hardware test.

The parent workspace's `build/edid-preferred-test/` contains the tested
SPI image, manifest and capture evidence. The five old-kernel trials
are in `build/edid-old-kernel-20260925-1653/`, each with a screenshot,
serial log, dmesg and DRM state. Earlier U-Boot image records are
historical and do not identify the current coreboot firmware.

## Capture

Start OBS or continuous capture before booting. The tested MACROSILICON
card asserts HPD and serves its EDID only while capture is active;
single-frame capture cannot keep it connected through a reboot.

```sh
capture=/dev/v4l/by-id/usb-MACROSILICON_USB3.0_UHD_20210623-video-index0
ffmpeg -f v4l2 -input_format yuyv422 -video_size 1280x720 \
  -framerate 30 -i "$capture" -vf fps=1 -q:v 3 \
  -update 1 -atomic_writing 1 -y hdmi-live.jpg
```

The USB capture size is independent of the HDMI input timing. Confirm
that timing through GOP or DRM state. Stop capture before returning
the device to OBS.

## Hardware references

The main reference is RevyOS `revyos/7.3.y`, commit
`59fb4a4d133d08cf46a28be07be58d8c7a82b870`: the open DC8200 driver in
`drivers/gpu/drm/verisilicon/`, `th1520-dw-hdmi.c`, the DW-HDMI core,
and TH1520 clock/reset drivers. The TH1520 vendor tree at
`https://github.com/revyos/th1520-linux-kernel.git`, commit
`a092d55649279e1c9bcda2769b8f6b4370fa2c94`, supplies a second reference
for cold-start and shadow-register handling. A210 sequences are not used.

Copied register definitions and adapted hardware sequences retain their
GPL licensing and attribution. Console integration uses the existing
GraphicsConsoleDxe, FrameBufferBltLib and BootLogoLib.
