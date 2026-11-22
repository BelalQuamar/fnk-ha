#!/usr/bin/env python3
"""Provision a connected controller. Secrets are prompted, never saved on the Mac."""
import argparse
import getpass
import json
import time
import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='USB serial port (autodetected if omitted)')
    args = parser.parse_args()
    port = args.port
    if not port:
        ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
        if len(ports) != 1:
            parser.error('Connect one ESP32, or specify --port. Ports: ' + ', '.join(p.device for p in list_ports.comports()))
        port = ports[0]
    config = {'action': 'configure'}
    config['ssid'] = input('2.4 GHz Wi-Fi name: ').strip()
    config['password'] = getpass.getpass('Wi-Fi password (blank for open network): ')
    config['url'] = input('Local Home Assistant URL (e.g. http://192.168.1.10:8123): ').strip()
    config['token'] = getpass.getpass('Home Assistant long-lived access token: ').strip()
    if any(not config[key] for key in ('ssid', 'url', 'token')):
        parser.error('Wi-Fi name, Home Assistant URL, and token are required')
    if not config['url'].startswith('http://'):
        parser.error('Enter a local http:// URL')
    # Avoid toggling boot/reset lines when opening native USB CDC.
    with serial.Serial(port=None, baudrate=115200, timeout=1) as device:
        device.dtr = False
        device.rts = False
        device.port = port
        device.open()
        time.sleep(0.5)
        device.reset_input_buffer()
        payload = (json.dumps(config, separators=(',', ':'), ensure_ascii=False) + '\n').encode()
        if len(payload) > 3501:
            parser.error('Setup message exceeds the firmware limit')
        # Pace setup writes for controllers that still have the small USB buffer.
        for offset in range(0, len(payload), 64):
            device.write(payload[offset:offset + 64])
            device.flush()
            time.sleep(0.04)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            reply = device.readline().decode(errors='replace').strip()
            if reply == 'SETUP_SAVED':
                print('Saved on ESP32. It is restarting and connecting to Home Assistant.')
                return
            if reply.startswith('SETUP_ERROR'):
                raise SystemExit(reply)
        raise SystemExit('No setup acknowledgement. Check the port and installed firmware.')


if __name__ == '__main__':
    try:
        main()
    except (serial.SerialException, KeyboardInterrupt) as error:
        raise SystemExit(str(error))
