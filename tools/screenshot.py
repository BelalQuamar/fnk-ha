#!/usr/bin/env python3
"""Capture the controller framebuffer over USB without additional image packages."""
import argparse
import json
import struct
import time
import zlib
from pathlib import Path
import serial


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/cu.usbmodem1101')
    parser.add_argument('--output', default='/tmp/freenove-ui.png')
    args = parser.parse_args()
    with serial.Serial(port=None, baudrate=115200, timeout=3) as device:
        device.dtr = False
        device.rts = False
        device.port = args.port
        device.open()
        time.sleep(.3)
        device.reset_input_buffer()
        # Opening the native USB port can reset some boards. Wait for discovery
        # so the capture represents the populated interface rather than boot.
        deadline = time.monotonic() + 55
        while time.monotonic() < deadline:
            device.write(b'{"action":"info"}\n')
            device.flush()
            for _ in range(5):
                line = device.readline().decode(errors='replace').strip()
                if line.startswith('{'):
                    info = json.loads(line)
                    if info.get('channels', 0) or info.get('demo'):
                        deadline = 0
                    break
            if deadline == 0:
                break
            time.sleep(1)
        device.reset_input_buffer()
        device.write(b'{"action":"screenshot"}\n')
        device.flush()
        for _ in range(15):
            header = device.readline().decode(errors='replace').strip()
            if header == 'SCREENSHOT 240 320 RGB565_LE':
                break
        else:
            raise SystemExit('No screenshot header received')
        pixels = device.read(240 * 320 * 2)
    if len(pixels) != 240 * 320 * 2:
        raise SystemExit('Incomplete screenshot')
    scanlines = bytearray()
    for y in range(320):
        scanlines.append(0)
        for x in range(240):
            value = struct.unpack_from('<H', pixels, (y * 240 + x) * 2)[0]
            r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
            scanlines.extend(((r * 255) // 31, (g * 255) // 63, (b * 255) // 31))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(scanlines)))
    png += chunk(b'IEND', b'')
    Path(args.output).write_bytes(png)
    print('Screenshot saved:', args.output)


if __name__ == '__main__':
    main()
