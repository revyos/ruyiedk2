#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check a TH1520 board USB graph and the DTB actually embedded in the firmware."""
import hashlib
from pathlib import Path
import struct
import sys
import libfdt

USB = '/soc/usb@ffe7040000'
BOARDS = {
    'meles': ('milkv,meles', ('usb5e3,610', 'usb5e3,620'), 3300000,
              '/regulator-usb-vbus', '/soc/gpio@ffec005000/gpio-controller@0', 27),
    'lpi4a': ('sipeed,lichee-pi-4a', ('usb2109,2817', 'usb2109,817'), 5000000,
              '/regulator-vcc5v-usb', '/soc/gpio@ffec006000/gpio-controller@0', 22),
}


def board_config(f):
    compatible = bytes(f.getprop(0, 'compatible')).split(b'\0')
    for name, config in BOARDS.items():
        if config[0].encode() in compatible:
            return name, config
    raise ValueError('unsupported USB board')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(blob):
    f = libfdt.Fdt(blob)
    board, config = board_config(f)
    _, ids, core_uv, vbus_path, gpio_path, gpio_pin = config
    node = f.path_offset
    prop = f.getprop
    target = lambda n, p: f.node_offset_by_phandle(prop(n, p).as_uint32())
    require(prop(node(USB), 'dr_mode').as_str() == 'host', 'USB must be host')
    require(f.hasprop(node(USB), 'snps,dis_u2_susphy_quirk'), 'missing SUSPHY quirk')
    hubs = [node(USB + '/hub@1'), node(USB + '/hub@2')]
    supply = target(hubs[0], 'vdd-supply')
    require(prop(supply, 'regulator-min-microvolt').as_uint32() == core_uv,
            'wrong hub core supply voltage')
    require(prop(supply, 'regulator-max-microvolt').as_uint32() == core_uv,
            'incorrect core voltage')
    require(f.hasprop(supply, 'regulator-always-on'), 'core rail must stay on')
    require(f.hasprop(supply, 'regulator-boot-on'), 'core rail is already on')
    for speed, hub in enumerate(hubs):
        require(prop(hub, 'compatible').as_str() == ids[speed],
                'wrong companion hub ID')
        require(prop(hub, 'reg').as_uint32() == speed + 1, 'wrong root port')
        require(target(hub, 'peer-hub') == hubs[1 - speed], 'peer link is not reciprocal')
        require(target(hub, 'vdd-supply') == supply, 'peers must share core power')
        require(not f.hasprop(hub, 'reset-gpios'), 'board has no software-controlled hub reset')
        require(not f.hasprop(hub, 'status') or prop(hub, 'status').as_str() == 'okay',
                'do not hide either companion hub')
        for port in range(1, 5):
            endpoint = node(f'{USB}/hub@{speed + 1}/ports/port@{port}/endpoint')
            connector = node(f'/connector-usb-a-{port}')
            remote = node(f'/connector-usb-a-{port}/ports/port@{speed}/endpoint')
            require(target(endpoint, 'remote-endpoint') == remote and
                    target(remote, 'remote-endpoint') == endpoint, 'broken port graph')
            require(prop(connector, 'compatible').as_str() == 'usb-a-connector',
                    'missing USB-A connector')
            vbus = target(connector, 'vbus-supply')
            require(vbus != supply, 'connector VBUS must not control hub core power')
            require(prop(vbus, 'regulator-min-microvolt').as_uint32() == 5000000,
                    'connector VBUS must be 5 V')
            require(f.hasprop(vbus, 'regulator-boot-on') and
                    f.hasprop(vbus, 'regulator-always-on'), 'VBUS handoff must retain power')
            gpio, pin, flags = struct.unpack('>III', prop(vbus, 'gpio'))
            require(f.get_path(f.node_offset_by_phandle(gpio)) ==
                    gpio_path and pin == gpio_pin and flags == 0,
                    'wrong connector VBUS GPIO')
    if board == 'lpi4a':
        gpio, pin, flags = struct.unpack('>III', prop(supply, 'gpio'))
        require(f.get_path(f.node_offset_by_phandle(gpio)) ==
                '/soc/i2c@ffec014000/gpio@18' and pin == 3 and flags == 0,
                'LPi4A hub must use PCA9557 output 3')
    return f


def embedded(image):
    fit = libfdt.Fdt(image[0x40000:])
    config = fit.getprop(fit.path_offset('/configurations'), 'default').as_str()
    require(config in BOARDS, 'unknown FIT configuration')
    name = fit.getprop(fit.path_offset('/configurations/' + config), 'fdt').as_str()
    n = fit.path_offset('/images/' + name)
    offset = fit.getprop(n, 'data-position').as_uint32()
    size = fit.getprop(n, 'data-size').as_uint32()
    require(offset >= fit.totalsize() and size > 0 and
            offset + size <= len(image) - 0x40000, 'DTB exceeds image')
    blob = image[0x40000 + offset:0x40000 + offset + size]
    digest = fit.getprop(fit.subnode_offset(n, 'hash'), 'value')
    require(hashlib.sha256(blob).digest() == digest, 'embedded DTB hash mismatch')
    require(board_config(libfdt.Fdt(blob))[0] == config, 'FIT/DT board mismatch')
    return blob


def regressions(blob):
    _, config = board_config(libfdt.Fdt(blob))
    vbus_path = config[3]
    changes = [
        (USB + '/hub@1', 'peer-hub', 'self'),
        (USB + '/hub@2', 'vdd-supply', 'vbus'),
        (USB + '/hub@2', 'status', 'disabled'),
        (vbus_path, 'regulator-boot-on', None),
        (vbus_path, 'regulator-always-on', None),
        ('/connector-usb-a-1/ports/port@0/endpoint', 'remote-endpoint', None),
    ]
    for path, key, value in changes:
        f = libfdt.Fdt(blob)
        f.resize(len(blob) + 512)
        n = f.path_offset(path)
        if value == 'self':
            f.setprop_u32(n, key, f.get_phandle(n))
        elif value == 'vbus':
            f.setprop_u32(n, key, f.get_phandle(f.path_offset(vbus_path)))
        elif value is None:
            f.delprop(n, key)
        else:
            f.setprop_str(n, key, value)
        try:
            check(f.as_bytearray())
        except (ValueError, libfdt.FdtException):
            continue
        raise ValueError(f'failed to reject unsafe graph: {path} {key}')


if __name__ == '__main__':
    require(len(sys.argv) == 3, 'usage: edk2-usb-dt.py board.dtb firmware.bin')
    dtb = Path(sys.argv[1]).read_bytes()
    check(dtb)
    packed = embedded(Path(sys.argv[2]).read_bytes())
    require(packed == dtb, 'firmware contains a stale DTB')
    check(packed)
    regressions(dtb)
    print('TH1520 USB DT: companion hubs, four connector pairs, power and packed DTB passed')
