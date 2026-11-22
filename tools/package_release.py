#!/usr/bin/env python3
"""Package build outputs, never device flash, into checksummed release assets."""
import argparse
import configparser
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
ENVIRONMENT = 'fnk-ha'
FLASH_IMAGES = ((0x0000, 'bootloader.bin'), (0x8000, 'partitions.bin'),
                (0xE000, 'boot_app0.bin'), (0x10000, 'firmware.bin'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package(version, output=None):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', version):
        raise ValueError('Version must be a simple tag such as v0.1')
    build = PROJECT / '.pio' / 'build' / ENVIRONMENT
    config = configparser.ConfigParser()
    config.read(PROJECT / 'platformio.ini')
    core = Path(config['platformio'].get('core_dir', '.pio-core'))
    if not core.is_absolute():
        core = PROJECT / core
    packages = core / 'packages'
    esptool = packages / 'tool-esptoolpy' / 'esptool.py'
    boot_app = packages / 'framework-arduinoespressif32' / 'tools' / 'partitions' / 'boot_app0.bin'
    images = [(offset, boot_app if name == 'boot_app0.bin' else build / name)
              for offset, name in FLASH_IMAGES]
    for _, path in images:
        if not path.is_file():
            raise FileNotFoundError(f'Missing build input: {path.name}. Run pio run first.')
    if not esptool.is_file():
        raise FileNotFoundError('PlatformIO esptool is missing; run pio run first.')
    # Match the tested ESP32-S3 DIO / 80 MHz / 16 MB boot image.
    header = images[0][1].read_bytes()[:4]
    if header[0] != 0xE9 or header[2] != 2 or header[3] != 0x4F:
        raise ValueError('Unexpected bootloader configuration; inspect board flash settings.')
    for (offset, path), (next_offset, _) in zip(images, images[1:]):
        if offset + path.stat().st_size > next_offset:
            raise ValueError('Flash images overlap')
    destination = Path(output) if output else PROJECT / 'dist' / version
    destination.mkdir(parents=True, exist_ok=True)
    stem = f'fnk-ha-{version}'
    merged = destination / f'{stem}-merged.bin'
    app = destination / f'{stem}-app.bin'
    command = [sys.executable, str(esptool), '--chip', 'esp32s3', 'merge_bin',
               '--output', str(merged)]
    for offset, path in images:
        command.extend((hex(offset), str(path)))
    subprocess.run(command, check=True)
    shutil.copyfile(build / 'firmware.bin', app)
    # Verify the merged offsets and that NVS is blank padding, not account data.
    combined = merged.read_bytes()
    for offset, path in images:
        raw = path.read_bytes()
        if combined[offset:offset + len(raw)] != raw:
            raise ValueError(f'Merged image verification failed: {path.name}')
    if combined[0x9000:0xE000] != b'\xff' * (0xE000 - 0x9000):
        raise ValueError('NVS range must contain only erased padding')
    if len(combined) != 0x10000 + app.stat().st_size:
        raise ValueError('Merged image length does not match the app offset')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=PROJECT, text=True).strip()
    manifest = {
        'version': version, 'source_commit': commit,
        'target': 'Freenove FNK0104A/B 2.8-inch ESP32-S3; 16 MB flash',
        'flash_mode': 'dio', 'flash_frequency': '80MHz', 'flash_size': '16MB',
        'images': [
            {'file': merged.name, 'offset': '0x0', 'sha256': digest(merged),
             'fresh_install': True, 'overwrites_saved_settings': True},
            {'file': app.name, 'offset': '0x10000', 'sha256': digest(app),
             'fresh_install': False, 'overwrites_saved_settings': False},
        ],
        'contains_device_flash_dump': False,
    }
    metadata = destination / 'manifest.json'
    metadata.write_text(json.dumps(manifest, indent=2) + '\n')
    instructions = destination / 'INSTALL.md'
    instructions.write_text(f'''# fnk-ha {version}: installation

Target: Freenove FNK0104A/B, 2.8-inch ILI9341, ESP32-S3 with 16 MB flash.
Other screen/controller variants are not supported by these binaries.

## Verify your download

From this release directory on macOS/Linux:

```sh
shasum -a 256 -c SHA256SUMS.txt
```

The source commit and image offsets are recorded in `manifest.json`.
All images are assembled from compiler outputs, never read from a configured board.

## Install the flashing tool

```sh
python -m pip install esptool==4.9.0
```

Replace `YOUR_USB_PORT` below with the board's serial port, such as
`/dev/cu.usbmodem1101`, `/dev/ttyACM0`, or `COM5`. Close any serial monitor first.

## Fresh install: merged image at 0x0

**This image overwrites existing firmware and saved Wi-Fi and Home Assistant settings.**
It includes the bootloader, partition table, OTA boot data, and application.
The padding across the NVS area clears existing settings. Re-provision afterward.

```sh
python -m esptool --chip esp32s3 --port YOUR_USB_PORT --baud 460800 write_flash 0x0 {merged.name}
```

## Update an existing installation: app image at 0x10000

Use only when this project's bootloader and partition layout are already installed.
It writes just the application and leaves the existing NVS settings untouched.

```sh
python -m esptool --chip esp32s3 --port YOUR_USB_PORT --baud 460800 write_flash 0x10000 {app.name}
```

Do not flash the app-only file at 0x0 or the merged file at 0x10000.
No full-chip erase is needed for either command.

## Configure a fresh installation

Download the source for the same release, install its host dependencies, and run:

```sh
python -m pip install -r requirements-dev.txt
python tools/configure.py --port YOUR_USB_PORT
```

Enter your Wi-Fi details, local Home Assistant HTTP URL, and long-lived access token. These
binaries contain no credentials. See the repository README for Home Assistant setup.
''')
    notes = PROJECT / 'docs' / 'releases' / f'{version}.md'
    release_notes = destination / 'RELEASE_NOTES.md'
    release_notes.write_text(notes.read_text() if notes.exists() else
                            f'# fnk-ha {version}\n\nDevelopment build. See INSTALL.md for flashing instructions.\n')
    files = [merged, app, metadata, instructions, release_notes]
    checksums = destination / 'SHA256SUMS.txt'
    checksums.write_text(''.join(f'{digest(path)}  {path.name}\n' for path in files))
    print(f'Packaged {version}: {destination}')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    package(args.version, args.output)


if __name__ == '__main__':
    main()
