# Changelog

All notable changes to P4 Recorder are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Binaries for each version are attached to its
[GitHub release](https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/releases).

## [Unreleased]

### Known issues

- Short blue flashes on the display during boot, before the SD-Card block appears on the home screen
  ([#1](https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/issues/1)).

## [0.5.1] - 2026-11-13

### Fixed

- The display no longer flashes blue during firmware updates. On this board every flash erase disturbs
  the display, so updates (from GitHub and from the web page) now erase the firmware area once, at the
  start, with the backlight off for a few seconds, and then only write. The About screen shows
  "Preparing…" during that step.

### Changed

- Firmware uploads from the web page are faster (about 35 s instead of 60 s).

## [0.5] - 2026-11-13

First stable release with updates from GitHub. Same code as 0.5-rc1.

## [0.5-rc1] - 2026-11-13 (pre-release)

### Added

- Firmware updates from GitHub: Settings → About → **Firmware update** checks this repository's latest
  release and, when it is newer than the installed version, downloads and installs its `-ota.bin` file
  over Wi-Fi (HTTPS). Settings and recordings are kept. Pre-releases are not offered.
- `firmware/tools/make_release.sh`: builds the full image, the OTA file, the license package and the
  checksums for a release.
- Host tests for version comparison (`fwversion`), where release candidates sort before the release.

### Changed

- TLS buffers are allocated in PSRAM.

## [0.4] - 2026-11-13

First public release.

### Added

- One-tap recording (or with the BOOT button), with level meter, adjustable gain and automatic
  screen-off while recording.
- Recording formats: WAV, Opus or MP3, with a 100 Hz high-pass filter.
- Power-loss protection: the file is closed every 2 s.
- Recording list on the device: details, rename, delete one or several; optional automatic deletion of
  the oldest recordings when the card is full.
- Web page on the local network to play, download, rename and delete recordings, with login and a
  lockout after five wrong passwords.
- Hotspot mode with a QR code to join, USB drive mode for the SD-Card, and a hidden `CONFIG.JSON` on the
  card that mirrors all settings.
- Firmware updates over Wi-Fi (web page or `firmware/tools/ota.sh`) with automatic rollback, and update
  of the ESP32-C6 Wi-Fi chip from the device.
- Screen lock with a numeric code, light/dark theme, English and Portuguese built in, extra languages
  from the card.
- Time from NTP or from the browser; cards in an unrecognized format are never formatted without
  confirmation.
- About screen with firmware, Wi-Fi chip, processor and memory information.

[Unreleased]: https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/compare/v0.5.1...HEAD
[0.5.1]: https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/compare/v0.5...v0.5.1
[0.5]: https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/compare/v0.5-rc1...v0.5
[0.5-rc1]: https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/compare/v0.4...v0.5-rc1
[0.4]: https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/releases/tag/v0.4
