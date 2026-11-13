# Board validation

Tests run on the Guition JC4880P443C_I_W_Y (ESP32-P4 rev 1.3 + ESP32-C6) to confirm it works as a
recorder. Useful for anyone with the same board.

## Ports and buttons

- **Two USB-C ports.** One is the P4's USB Serial/JTAG: it flashes the firmware and shows the console. The
  other is the P4's native high-speed USB, used in USB drive mode.
- **Buttons** (looking at the back of the board, with the microSD slot at the top): in the top-left corner,
  **BOOT (SW1)** on the left and **RST (SW2)** on the right. BOOT also starts and stops recording. The third
  button (SW3), next to the battery connector, is the IP5306 key: it turns the board on and off when running
  on battery.
- The LED on GPIO26 mentioned in some community material does not light up on this board.

## Peripherals

| Item | Result | Notes |
|---|---|---|
| Display | works | ST7701S over MIPI-DSI (2 lanes, 500 Mbps), LDO channel 3 at 2.5 V, LVGL 9 |
| Touch | works | GT911 at 0x5D, on the same I²C bus as the codec (SDA 7, SCL 8) |
| microSD | works | 4-bit SDMMC, slot 0 (GPIO 39–44), powered by LDO channel 4 |
| Wi-Fi | works | ESP32-C6 over SDIO (ESP-Hosted). Shipped with firmware 2.3.2; the recorder updates it to 2.12.13 from the About screen |
| USB drive mode | works | the card shows up on the computer; when ejected, it goes back to the device. Disabled while recording |

## Audio

| Item | Result | Notes |
|---|---|---|
| On-board microphone | yes | the ES8311 codec (I²C 0x18) has a soldered microphone. Silence ≈ −64 dBFS; speech at 30 cm peaks up to −4 dBFS with 30 dB gain |
| Lossless recording | works | 60 s recorded result in 60.0 s in the file, with no dropped blocks |
| Power loss | works | the file is closed every 2 s: 15 s recorded before a reset resulted in 14 s recovered |
| Full card | works | the current recording is closed with a valid header and the device shows a warning |
| Speech at 30 cm and 1 m | works | automatic transcription (Whisper, on the computer) is correct |
| 2-minute conversation with several people | works | no clipping; transcription coherent from start to finish |
| Speakerphone call | works | transcription on par with a recording made with a laptop microphone |
| Real-time Opus and MP3 | works | the P4 encodes without losing audio (Opus via `esp_audio_codec` 2.5; MP3 via Shine) |

Default gain: 30 dB worked from 30 cm to 1 m without clipping.

## Manufacturer specification

- Typical consumption: about 320 mA at 5 V.
- Battery: 2-pin MX 1.25 connector, charged by the IP5306. Check the polarity before connecting a cell.

## Flash erase and the display

Every flash erase makes the display flash light blue for its duration (64 KB blocks: slow flashing; 4 KB
sectors: fast flashing). Flash writes without erasing, Wi-Fi/HTTPS traffic and screen redraws do not.
The DSI bridge reports no underrun, and running the code from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`)
does not help. Firmware updates therefore erase the whole image area once, at the start, with the
backlight off, and then only write.

## Not measured yet

- Actual consumption with the screen on and off (to choose a battery).
- Continuous recording of 30 minutes or more.
- Conversation at 3 m.
