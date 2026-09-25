# Device tree

EDK2 receives a DTB from the preceding boot stage and exposes it to
platform drivers through the FDT HOB. Meles and LPi4A use Linux board
bindings for wiring and resources. Shared TH1520 drivers implement the
SoC register layout and initialization sequences.

## Resource parsing

`Th1520BoardSupportLib` parses:

- `status`, `compatible`, `reg` and provider phandles.
- Default pinctrl pins, functions, drive strength, bias and input settings.
- DW GPIO `gpio-ranges` and PCA9557 I2C addresses and pins.
- `regulator-fixed` GPIOs, polarity, startup delays and `vin-supply`.
- `pwm-fan` controller, channel, period, polarity and optional supply.
- Supply references for USB PHYs, hubs and connectors.

Power setup programs GPIO values and directions before selecting the
pin mux. Shared supplies are deduplicated by phandle. USB role changes
switch routing while keeping hub power and VBUS on. Fixed regulators
follow the binding's `enable-active-high` polarity.

The fan runs at full duty during boot. Its period is calculated from
the inherited PWM clock without retuning the PLL. GMAC uses the same
pinctrl helpers alongside controller and shared MDIO discovery.

## Scope and board policy

The parser supports TH1520 direct address mappings, the relevant pins
in `pinctrl-0`, and fixed regulators. It validates power plans and
pinctrl states before register writes and rejects unknown controllers,
pins or formats. Early resource errors are logged to serial; startup
continues so recovery remains accessible.

Host/device USB routing belongs to the board library. Static
`gpio-hog` entries do not describe the levels required for each role,
so they cannot define Fastboot role switching. Boot hotkeys and default
boot selection are firmware policy rather than device-tree properties.

## Validation

Run host checks against the boards' actual DTBs:

```sh
make edk2-meles-usb-check edk2-lpi4a-usb-check edk2-gmac-check
```

DTBs default to the parent workspace's `../build` directory. Checks
cover power sequencing, shared hub supplies, role switching, PWM
parameters and invalid references.

Meles 16 GiB hardware validation on 2026-09-22 confirmed:

- PWM2 period and duty were both 1,250,000 ticks at the inherited
  125 MHz clock, matching the DT's 10 ms period and full duty.
- Both GL3523 peer hubs and USB storage enumerated.
- eMMC read at about 41.7 MiB/s and USB at about 38.3 MiB/s; two
  64 MiB reads of each device produced matching checksums.
- GMAC SNP loopback passed 16,384 frames.
- Linux handoff and normal restart passed.

LPi4A passed compilation and host checks with its actual DTB. That
validation session did not include flashing or hardware tests on LPi4A.
