#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Read/write named Fastboot media as opaque bytes, with bounded transfers."""
import argparse
import ctypes as c
from pathlib import Path


class Descriptor(c.Structure):
    _fields_ = [('length', c.c_uint8), ('type', c.c_uint8), ('usb', c.c_uint16),
                ('class_', c.c_uint8), ('subclass', c.c_uint8), ('protocol', c.c_uint8),
                ('packet', c.c_uint8), ('vendor', c.c_uint16), ('product', c.c_uint16),
                ('device', c.c_uint16), ('manufacturer', c.c_uint8),
                ('product_string', c.c_uint8), ('serial', c.c_uint8), ('configs', c.c_uint8)]


class Fastboot:
    def __init__(self, serial):
        self.u = u = c.CDLL('libusb-1.0.so.0')
        self.ctx, self.handle = c.c_void_p(), c.c_void_p()
        u.libusb_init.argtypes = [c.POINTER(c.c_void_p)]
        u.libusb_get_device_list.argtypes = [c.c_void_p, c.POINTER(c.POINTER(c.c_void_p))]
        u.libusb_get_device_list.restype = c.c_ssize_t
        u.libusb_free_device_list.argtypes = [c.POINTER(c.c_void_p), c.c_int]
        u.libusb_get_device_descriptor.argtypes = [c.c_void_p, c.POINTER(Descriptor)]
        u.libusb_open.argtypes = [c.c_void_p, c.POINTER(c.c_void_p)]
        u.libusb_get_string_descriptor_ascii.argtypes = [c.c_void_p, c.c_uint8, c.c_void_p, c.c_int]
        u.libusb_claim_interface.argtypes = [c.c_void_p, c.c_int]
        u.libusb_release_interface.argtypes = [c.c_void_p, c.c_int]
        u.libusb_close.argtypes = [c.c_void_p]
        u.libusb_exit.argtypes = [c.c_void_p]
        u.libusb_bulk_transfer.argtypes = [c.c_void_p, c.c_uint8, c.c_void_p,
                                         c.c_int, c.POINTER(c.c_int), c.c_uint]
        self.check(u.libusb_init(c.byref(self.ctx)))
        devices = c.POINTER(c.c_void_p)()
        count = u.libusb_get_device_list(self.ctx, c.byref(devices))
        try:
            self.check(count if count < 0 else 0)
            for i in range(count):
                d = Descriptor()
                self.check(u.libusb_get_device_descriptor(devices[i], c.byref(d)))
                if d.vendor != 0x18d1 or d.product not in (0x4ee0, 0xd002):
                    continue
                handle = c.c_void_p()
                if u.libusb_open(devices[i], c.byref(handle)) < 0:
                    continue
                buf = c.create_string_buffer(256)
                n = u.libusb_get_string_descriptor_ascii(handle, d.serial, buf, len(buf))
                if n > 0 and buf.raw[:n].decode() == serial:
                    self.handle = handle
                    break
                u.libusb_close(handle)
            if not self.handle:
                raise RuntimeError('Fastboot serial not found: ' + serial)
            self.check(u.libusb_claim_interface(self.handle, 0))
        except BaseException:
            u.libusb_free_device_list(devices, 1)
            devices = None
            self.close()
            raise
        finally:
            if devices is not None:
                u.libusb_free_device_list(devices, 1)

    @staticmethod
    def check(code):
        if code < 0:
            raise RuntimeError('libusb error ' + str(code))

    def close(self):
        if self.handle:
            self.u.libusb_release_interface(self.handle, 0)
            self.u.libusb_close(self.handle)
            self.handle = c.c_void_p()
        if self.ctx:
            self.u.libusb_exit(self.ctx)
            self.ctx = c.c_void_p()

    def send(self, data):
        buf, done = c.create_string_buffer(data), c.c_int()
        self.check(self.u.libusb_bulk_transfer(self.handle, 1, buf, len(data), c.byref(done), 120000))
        if done.value != len(data):
            raise RuntimeError('short USB write')

    def receive(self, size=64):
        buf, done = c.create_string_buffer(size), c.c_int()
        self.check(self.u.libusb_bulk_transfer(self.handle, 0x81, buf, size, c.byref(done), 120000))
        if not done.value:
            raise RuntimeError('empty USB response')
        return buf.raw[:done.value]

    def response(self):
        data = self.receive()
        while data.startswith(b'INFO'):
            print(data[4:].decode(errors='replace'), flush=True)
            data = self.receive()
        if data.startswith(b'FAIL'):
            raise RuntimeError(data[4:].decode(errors='replace'))
        return data

    def command(self, command):
        data = command.encode()
        if len(data) > 64:
            raise ValueError('Fastboot command exceeds 64 bytes')
        self.send(data)
        return self.response()

    def getvar(self, name):
        reply = self.command('getvar:' + name)
        if not reply.startswith(b'OKAY'):
            raise RuntimeError('unexpected getvar response: ' + repr(reply))
        return reply[4:].decode()

    def read(self, medium, offset, length):
        reply = self.command(f'fetch:{medium}:{offset:x}:{length:x}')
        if not reply.startswith(b'DATA') or int(reply[4:], 16) != length:
            raise RuntimeError('unexpected read response: ' + repr(reply))
        data = bytearray()
        while len(data) < length:
            data.extend(self.receive(min(16384, length - len(data))))
        if self.response() != b'OKAY':
            raise RuntimeError('read did not complete')
        return data

    def write(self, medium, offset, data):
        reply = self.command(f'download:{len(data):08x}')
        if not reply.startswith(b'DATA') or int(reply[4:], 16) != len(data):
            raise RuntimeError('unexpected download response: ' + repr(reply))
        for pos in range(0, len(data), 16384):
            self.send(data[pos:pos + 16384])
        if self.response() != b'OKAY':
            raise RuntimeError('download did not complete')
        if self.command(f'flash:{medium}:{offset:x}') != b'OKAY':
            raise RuntimeError('write did not complete')


def integer(text):
    number = int(text, 0)
    if number < 0:
        raise argparse.ArgumentTypeError('negative byte range')
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    commands = parser.add_subparsers(dest='command', required=True)
    variables = commands.add_parser('getvar')
    variables.add_argument('name')
    for name in ('read', 'write'):
        action = commands.add_parser(name)
        action.add_argument('medium', choices=('spi', 'emmc0', 'emmc0boot0', 'emmc0boot1'))
        action.add_argument('file', type=Path)
        action.add_argument('--offset', type=integer, default=0)
        if name == 'read':
            action.add_argument('--length', type=integer)
    args = parser.parse_args()
    device = Fastboot(args.serial)
    try:
        if args.command == 'getvar':
            print(device.getvar(args.name))
            return
        capacity = int(device.getvar('partition-size:' + args.medium), 16)
        length = (args.file.stat().st_size if args.command == 'write' else
                  args.length if args.length is not None else capacity - args.offset)
        if args.offset > capacity or length <= 0 or length > capacity - args.offset:
            raise ValueError('byte range exceeds medium capacity')
        # Chunk bytes without parsing, expanding or repackaging their contents.
        with args.file.open('rb' if args.command == 'write' else 'wb') as f:
            done = 0
            while done < length:
                count = min(16 * 1024 * 1024, length - done)
                if args.command == 'write':
                    data = f.read(count)
                    if len(data) != count:
                        raise RuntimeError('input changed or truncated during write')
                    device.write(args.medium, args.offset + done, data)
                else:
                    f.write(device.read(args.medium, args.offset + done, count))
                done += count
                print(f'{args.medium}: {done}/{length} bytes', flush=True)
    finally:
        device.close()


if __name__ == '__main__':
    main()
