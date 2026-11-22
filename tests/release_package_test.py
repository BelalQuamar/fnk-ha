#!/usr/bin/env python3
"""Independently verify a release's checksums and ESP32 flash-image layout."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def verify(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    merged_entry, app_entry = manifest['images']
    merged = (directory / merged_entry['file']).read_bytes()
    app = (directory / app_entry['file']).read_bytes()
    assert merged_entry['offset'] == '0x0'
    assert app_entry['offset'] == '0x10000'
    assert merged_entry['overwrites_saved_settings'] is True
    assert app_entry['overwrites_saved_settings'] is False
    assert not manifest['contains_device_flash_dump']
    assert merged[0] == 0xE9 and app[0] == 0xE9
    assert merged[2] == 2 and merged[3] == 0x4F # DIO, 16 MB, 80 MHz
    assert merged[0x10000:] == app
    assert merged[0x9000:0xE000] == b'\xff' * 0x5000
    partitions = {}
    for pos in range(0x8000, 0x8C00, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', merged[pos:pos+32])
        if magic != 0x50AA:
            break
        partitions[label.rstrip(b'\x00').decode()] = (kind, subtype, offset, size)
    assert partitions['nvs'][2:] == (0x9000, 0x5000)
    assert any(kind == 0 and offset == 0x10000 for kind, _, offset, _ in partitions.values())
    assert len(merged) <= 16 * 1024 * 1024
    for entry in manifest['images']:
        assert hashlib.sha256((directory / entry['file']).read_bytes()).hexdigest() == entry['sha256']
    checksums = (directory / 'SHA256SUMS.txt').read_text().splitlines()
    assert len(checksums) == 5
    for line in checksums:
        expected, filename = line.split('  ', 1)
        assert Path(filename).name == filename
        assert hashlib.sha256((directory / filename).read_bytes()).hexdigest() == expected
    print('Release checksums, partition table, offsets, NVS padding and image headers: passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    verify(parser.parse_args().directory)
