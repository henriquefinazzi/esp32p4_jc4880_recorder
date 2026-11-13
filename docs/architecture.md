# Firmware architecture

Overview of how the firmware is organized. Details of each part are in the code comments.

## Tasks

| Task | What it does |
|---|---|
| audio | reads the microphone (ES8311 over I²S, 16 kHz, mono, 16-bit) in 100 ms blocks, applies the high-pass filter and puts the blocks in an 8 s buffer in PSRAM |
| recording | takes blocks from the buffer and writes them in the chosen format (WAV, Opus or MP3); closes the file every 2 s; pinned to a core and given a large stack because of Opus |
| UI (LVGL) | screens, keyboard, notices; a 200 ms timer updates the clock, status and transitions |
| network | Wi-Fi through the ESP32-C6 (ESP-Hosted), automatic reconnection to saved networks, access point, NTP time |
| settings | mirrors the settings to `CONFIG.JSON` on the card and applies changes made there |
| web server | recordings page, API and firmware updates |

The 8 s buffer decouples reading the microphone from writing to the card: a card stall does not lose audio.

## Components (`firmware/components`)

- **audio_in / audio_dsp:** capture and filter.
- **wav_writer / rec_writer:** writing WAV, Opus (own Ogg packaging) and MP3 (Shine).
- **storage:** microSD card and USB drive mode (TinyUSB). The card has one owner at a time: the device or
  the computer. Uses a local copy of `esp_tinyusb` with a fix that prevents it from formatting a non-FAT
  card on its own (see `components/espressif__esp_tinyusb/PATCH.md`).
- **display:** display, touch and LVGL.
- **net:** Wi-Fi, saved networks, access point, NTP and ESP32-C6 updates.
- **settings_file:** reading and validating `CONFIG.JSON` (plain C, tested on the computer).
- **i18n:** texts by key, loaded from JSON files. English and Portuguese are built in, extra languages
  come from `/sdcard/LANGUAGES/<code>.json`, and missing texts fall back to English.

## Where settings live

Internal storage (NVS) is the source of truth; `CONFIG.JSON` on the card is an editable mirror. This way
the device boots configured even without a card. Invalid values in the file are ignored. The file's keys
are in Portuguese, kept that way for compatibility.

## Firmware and updates

The flash has two firmware slots (`ota_0` and `ota_1`). An update writes to the free slot, switches and
reboots; if the new firmware does not finish booting, the next reboot goes back to the previous one.

## Tests

`firmware/test_host` builds the plain-C parts for the computer and checks, among other things, that the
generated Opus and MP3 files open in `ffmpeg` with the correct duration.
