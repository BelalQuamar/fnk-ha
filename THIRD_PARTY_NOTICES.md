# Third-party notices

The root [MIT license](LICENSE) applies to this project's original firmware,
setup utilities, tests, and documentation. It does not replace the licenses of
third-party components.

| Component | Source and use | License / notice |
| --- | --- | --- |
| TFT_eSPI 2.5.43, Freenove distribution | Bundled in `lib/TFT_eSPI`; display rendering, fonts, and board support. Originally by Bodmer, distributed with Freenove's examples. | Preserve [the library notices](lib/TFT_eSPI/license.txt), source headers, and [GFX font notices](lib/TFT_eSPI/Fonts/GFXFF/license.txt). The library contains MIT and BSD notices. |
| Freenove board configuration | Display/touch pin assignments derived from [Freenove ESP32-S3 Display examples](https://github.com/Freenove/Freenove_ESP32_S3_Display). | Board documentation and downloaded reference resources remain upstream; the `vendor/` checkout is excluded from this repository. |
| IANA Time Zone Database | Generated UTC offset and abbreviation data in `include/clock_timezones.inc`. Regenerate with `tools/generate_timezones.py`. | Public domain. |
| ArduinoJson 6.21.5 | Downloaded by PlatformIO; Home Assistant API JSON parsing. | MIT; [upstream license](https://github.com/bblanchon/ArduinoJson/blob/v6.21.5/LICENSE.txt). |
| TJpg_Decoder 1.1.0 | Downloaded by PlatformIO to decode Home Assistant camera JPEGs. Includes ChaN’s Tiny JPEG Decompressor. | [Upstream license and notices](https://github.com/Bodmer/TJpg_Decoder/blob/master/license.txt). |
| Arduino-ESP32 / ESP-IDF | Downloaded by PlatformIO; Wi-Fi, HTTP, GPIO, NTP and NVS. | Upstream component-specific licenses apply; these packages are not vendored in this repository. |

Two local changes in the bundled TFT_eSPI distribution fix its
`USER_SETUP_LOADED` preprocessor guard and restrict its PlatformIO source filter
to `TFT_eSPI.cpp`, avoiding drivers for unrelated displays. Unused upstream
examples, documentation and desktop tools are excluded from Git.

Freenove, ESP32, and Home Assistant are names belonging to their respective
owners. This is an independent community project.
