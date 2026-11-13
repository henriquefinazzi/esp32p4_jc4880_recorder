# Third-party notices

This project's own code is under the MIT license ([LICENSE](LICENSE)). The items below are third-party
and keep their own licenses; the MIT license does not apply to them.

## Included in this repository

| Component | Location | License | License text |
|---|---|---|---|
| Shine (MP3 encoder), © the Shine authors (Gabriel Bouvigne, Pete Everett, Romain Beauxis and others), <https://github.com/toots/shine>, unmodified | `firmware/components/shine/` | LGPL-2.0-only (GNU Library General Public License v2) | `firmware/components/shine/COPYING` |
| esp_tinyusb 1.7.6~2, © Espressif Systems, <https://github.com/espressif/esp-usb>, **modified** (see `PATCH.md`) | `firmware/components/espressif__esp_tinyusb/` | Apache-2.0 | `firmware/components/espressif__esp_tinyusb/LICENSE` |
| `tusb_config.h` inside esp_tinyusb, © 2019 Ha Thach (tinyusb.org) | same as above | MIT | below |
| Montserrat font, © 2011 The Montserrat Project Authors, <https://github.com/JulietaUla/Montserrat> | `firmware/components/fonts/` | SIL OFL 1.1 | `firmware/components/fonts/OFL.txt` |
| ST7701S panel initialization sequence, from the BSP <https://github.com/ultramcu/guition-jc4880p4-bsp>, © 2026 ultramcu | `firmware/components/display/st7701_init.h` | MIT | `licenses/LICENSE-ultramcu-guition-bsp.txt` |
| ESP32-C6 firmware (binary), built from esp-hosted-mcu 2.12.13 (<https://github.com/espressif/esp-hosted-mcu>) with ESP-IDF 5.5.5 | `firmware/c6/network_adapter_esp32c6.bin` | Apache-2.0 (esp-hosted, ESP-IDF, Espressif Wi-Fi/PHY libraries); also includes FreeRTOS (MIT), wpa_supplicant (BSD-3-Clause), mbedTLS (Apache-2.0), protobuf-c (BSD-2-Clause), newlib (BSD licenses) and Espressif's BLE controller library, derived from Apache Mynewt NimBLE (Apache-2.0, see NOTICE) | `licenses/LICENSE-Apache-2.0.txt`, `licenses/NOTICE-esp32c6-bt-lib.txt` and the ESP-IDF copyright list: <https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/COPYRIGHT.html> |

### TinyUSB MIT license (`tusb_config.h`)

```
The MIT License (MIT)

Copyright (c) 2019 Ha Thach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Downloaded at build time (not in the repository)

PlatformIO and the ESP-IDF component manager download, among others:

- **ESP-IDF** and Espressif components (esp_hosted, esp_wifi_remote, esp_lvgl_port, esp_lcd_touch,
  esp_lcd_touch_gt911, esp_codec_dev, mdns, eppp_link, esp_serial_slave_link, wifi_remote_over_eppp):
  Apache-2.0. Inside ESP-IDF: lwIP (BSD-3), mbedTLS (Apache-2.0), protobuf-c (BSD-2), FreeRTOS (MIT).
- **LVGL**: MIT. **TinyUSB**: MIT. **cJSON**: MIT.
- **esp_audio_codec** (Espressif), used for Opus: *Espressif Modified MIT* license, which allows use
  **only with Espressif products**. Includes libopus (BSD-3).

## When distributing a compiled firmware

The compiled firmware contains Shine, under LGPL-2.0-only, statically linked. Anyone distributing
`firmware.bin` (for example, as a GitHub release asset or flashed onto a device) must:

1. state that the firmware contains Shine under the LGPL and include the license text (`COPYING`);
2. allow the user to modify Shine and rebuild the firmware. For a GitHub *release*, it is enough to
   publish the binary together with the exact tag/commit of this repository's source code (which includes
   Shine and `dependencies.lock`) and the build instructions (`pio run`) — LGPL §6(d). For a device given
   to someone, include the source code of that version (for example, a .zip of the tag) or a written offer,
   valid for at least 3 years, to provide it — LGPL §6(a)/(c). Do not prevent modification or reverse
   engineering for debugging such modifications;
3. include these notices and the license texts: MIT (this project, ultramcu, TinyUSB, LVGL, cJSON,
   FreeRTOS), Apache-2.0 (ESP-IDF, esp_hosted, esp_tinyusb, mbedTLS, mdns, esp_lcd_touch), BSD-3 (lwIP,
   libopus, wpa_supplicant), BSD-2 (protobuf-c), *Espressif Modified MIT* (esp_audio_codec — the
   component's `LICENSE` file), OFL 1.1 (Montserrat), LGPL-2.0 (Shine) and the NimBLE NOTICE; the full
   ESP-IDF list is at <https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/COPYRIGHT.html>.
   In practice: attach to each *release* a `licenses.zip` with these texts (they are in
   `firmware/managed_components/*/LICENSE*` after the first build and on the page above).
