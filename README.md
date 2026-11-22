# FNK_HA

FNK_HA is a Home Assistant controller for the **Freenove FNK0104B 2.8-inch ESP32-S3 touch display**. It connects directly to Home Assistant over local Wi-Fi and keeps the adaptive grid from ESP Smart Display.

[![Firmware CI and release](https://github.com/BelalQuamar/fnk-ha/actions/workflows/firmware.yml/badge.svg)](https://github.com/BelalQuamar/fnk-ha/actions/workflows/firmware.yml)

![FNK_HA screenshot captured over USB from the Freenove display while connected to Home Assistant](docs/device-screen.png)

## Install the firmware

These binaries are for the **Freenove FNK0104B 2.8-inch ILI9341 display with a 16 MB ESP32-S3**. Connect it to your computer with a USB data cable and close any open serial monitor.

Download the binaries, checksums, and flashing instructions from [v0.1 on GitHub](https://github.com/BelalQuamar/fnk-ha/releases/tag/v0.1). On macOS or Linux, verify the release files in the download directory with `shasum -a 256 -c SHA256SUMS.txt`.

| Use | Binary | Flash address |
| --- | --- | --- |
| First installation | `fnk-ha-v0.1-merged.bin` | `0x0` |
| Update an existing FNK_HA installation | `fnk-ha-v0.1-app.bin` | `0x10000` |

The merged image replaces the existing firmware and clears saved settings. The app image requires the FNK_HA partition layout and preserves saved configuration. Install esptool and flash the appropriate file:

    python -m pip install esptool==4.9.0
    python -m esptool --chip esp32s3 --port YOUR_USB_PORT --baud 460800 write_flash 0x0 fnk-ha-v0.1-merged.bin

For an app-only update, use `write_flash 0x10000 fnk-ha-v0.1-app.bin` in the same command. USB ports typically look like `/dev/cu.usbmodem1101` on macOS, `/dev/ttyACM0` on Linux, or `COM5` on Windows.

## Connect to Home Assistant

1. From a computer on your local network, open Home Assistant using its LAN IPv4 address and port, such as `http://192.168.1.10:8123`. Use an HTTP address reachable from the display's 2.4 GHz Wi-Fi network. HTTP sends the token in plaintext, so use a trusted LAN.
2. In Home Assistant, open your profile's **Security** tab and create a **Long-lived access token**.
3. Clone this repository or download and extract the source archive from the release. From the source directory, install the serial dependency and run setup with the display connected.

       python -m pip install pyserial==3.5
       python tools/configure.py --port YOUR_USB_PORT

4. Enter the Wi-Fi network name and password, the Home Assistant LAN URL, and the token. The setup script prompts for secrets without echoing them. When it reports **Saved on ESP32**, the board restarts and loads the entity list.

## Use the display

The view selector and favorites are available in current source builds. The v0.1 release uses the original all-entity grid.

| Control | What it does |
| --- | --- |
| Entity tile | Toggles an available light, switch, fan, humidifier, or input boolean. Other tiles display their reported states. |
| **Controls**, **All**, or **Favorites** | Tap the top-right button to cycle through views. **Controls** is the default and shows only the supported on/off entity types. |
| **<** and **>** | Move between pages. Each page fits up to nine tiles. |
| **ALL ON** and **ALL OFF** | Send the corresponding command to supported, available entities across every page of the selected view. |
| **REFRESH** | Reloads the list and reported states immediately. |

**All** shows every entity Home Assistant reports, sorted by friendly name. Names use smaller text on narrow tiles and scroll when they still exceed two lines. A device with a switch and a power sensor appears on two tiles. The display refreshes every 60 seconds, picking up new, renamed, and removed entities.

To customize the order, press and hold a tile for about a second and release in **Controls** or **All** to add or remove it from **Favorites**. A yellow dot marks a favorite. In **Favorites**, press and hold a tile and release to move it to the front. Repeat from the last device you want in the order to the first. The display saves your selected view and favorite order through restarts.

After two minutes of inactivity, the display sleeps. A tap or BOOT press wakes it. If touch is unavailable, briefly press BOOT to move focus or hold it for at least 0.8 seconds to activate the focused tile or button. Hold for at least 1.8 seconds for the favorite action. The clock uses network time and defaults to Toronto.

## Troubleshooting

- **DEMO** means the board needs USB setup. Run the setup script and wait for the save confirmation.
- **Wi-Fi offline** means the board has lost its Wi-Fi connection. Check the 2.4 GHz SSID, password, and signal.
- **Home Assistant unreachable** means the board received no HTTP response. Verify the URL in a browser on the same LAN. Use the server's LAN IP if its hostname fails to resolve and check for guest Wi-Fi isolation or VLAN rules.
- **HA token rejected** means Home Assistant responded but refused authentication. Create a fresh token and rerun setup.
- When a refresh fails, the previous list remains visible with offline tiles. The display retries automatically.

## Development

To build or upload from source, prepare a Python virtual environment, then run:

    python -m pip install -r requirements-dev.txt
    pio run
    pio run -t upload --upload-port YOUR_USB_PORT

Firmware uses the [Home Assistant REST API](https://developers.home-assistant.io/docs/api/rest/). The board's pin assignments are in [display_setup.h](include/display_setup.h). For USB framebuffer capture, run:

    python tools/screenshot.py --help

Run the host-side checks for layout, sleep timing, and timezone:

    for test in tests/*_test.cpp; do
      c++ -std=c++11 -Iinclude "$test" -o /tmp/fnk-ha-test && /tmp/fnk-ha-test || exit 1
    done
