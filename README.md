**English** · [Português](README.pt-BR.md)

# esp32p4-jc4880-recorder (P4 Recorder)

Portable meeting recorder with a touchscreen, built on the **Guition JC4880P443C_I_W_Y** board
(ESP32-P4 + ESP32-C6, 4.3" display). It records to microSD as WAV, Opus or MP3, shows the recordings
on a web page on the local network and works as a USB drive.

## What it does

- **One-tap recording** (or with the BOOT button), with an audio level meter, gain that can be adjusted
  while recording, and a screen that turns itself off to save power.
- **Formats:** WAV (uncompressed, ~115 MB/h), Opus (~11 MB/h) or MP3 (~14 MB/h).
- **Low-frequency noise filter** (100 Hz high-pass) against air-conditioning hum and knocks on the table.
- **Power-loss protection:** the file is closed every 2 s; a power cut loses at most that much.
- **Recording list** on the device: view details, rename, delete one or several.
- **Full card:** option to delete the oldest recordings automatically.
- **Web page** on the local network (`http://<name>.local`): play, download, rename and delete recordings,
  protected by a username and password. Five wrong passwords block the address for 5 minutes.
- **Hotspot (access point):** the recorder creates a Wi-Fi network so a phone can connect directly,
  without a router, with a QR code to join.
- **USB drive mode:** over the high-speed USB port, the card shows up on the computer. When ejected, it goes
  back to the device.
- **Settings on the card:** a hidden `CONFIG.JSON` file mirrors all settings and can be edited on a
  computer; the device applies the changes when the card comes back.
- **Firmware updates over Wi-Fi**: the device checks this repository's latest release and installs it
  (Settings → About → Firmware update), or an update can be sent from the web page or the command line.
  If the new version fails to boot, the device goes back to the previous one. The Wi-Fi chip (ESP32-C6) is also updated by the device.
- **Screen lock** with a numeric code and **light/dark theme**.
- **Languages:** English and Portuguese built in (the device starts in English); other languages can be
  added as files on the card, without rebuilding.
- **Time** from the internet (configurable NTP server) or from the browser when there is no internet.
- Cards in an unrecognized format (exFAT, NTFS) are **never formatted without confirmation**.

## Hardware

| Item | Detail |
|---|---|
| Board | Guition JC4880P443C_I_W_Y (ESP32-P4 with 32 MB PSRAM and 16 MB flash; ESP32-C6 for Wi-Fi) |
| Display | 4.3", 480 x 800, MIPI-DSI (ST7701S), GT911 touch |
| Audio | ES8311 codec with on-board microphone |
| Storage | microSD (FAT32) |
| Power | USB-C, or a lithium battery on the board connector (charged by the IP5306) |

The board has two USB-C ports: the **flashing/console** port (USB Serial/JTAG) and the **high-speed**
port, used in USB drive mode.

> The firmware was developed and tested on a board with ESP32-P4 **revision 1.3**. That is why
> `esp_audio_codec` is pinned to version 2.5.x: 2.6 and later require revision 3.0 or newer.

## Download

A ready-to-flash image is attached to each [release](https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/releases).
Connect the board's console USB-C port and use either option:

- **From the browser (no install):** open <https://web.esphome.io/> in Chrome or Edge, click **Connect**,
  pick the board's serial port, click **Install** and choose `p4-recorder-v<version>-full.bin`.
- **From the command line**, flashing at offset 0x0:

  ```bash
  esptool --chip esp32p4 write-flash 0x0 p4-recorder-v<version>-full.bin
  ```

The full image erases the device's saved settings; recordings on the SD-Card are kept.

Once the device runs version 0.5 or later, the next versions can be installed from the device itself:
**Settings → About → Firmware update** checks the latest release and, when there is a newer one, downloads
the `-ota.bin` file attached to it over Wi-Fi. Settings are kept. Pre-releases (such as `0.5-rc1`) are not
offered there; install them by hand.

## Build and flash

Requirements: [PlatformIO](https://platformio.org/) (the project uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform with ESP-IDF 5.5).

```bash
cd firmware
pio run                                   # build
pio run -t upload --upload-port <port>    # flash over the console USB (first time)
```

To keep build files outside the project folder, set `PLATFORMIO_WORKSPACE_DIR`.

The first flash must be over USB, because it also writes the partition table with the two firmware
slots. After that, updates can go over Wi-Fi:

```bash
firmware/tools/ota.sh <hostname>          # builds and uploads; asks for the browser password
```

You can also use the recorder's web page, under **Firmware → Update firmware…** (shown when developer
functions are turned on, in Settings → Developer mode).

## First use

1. Insert a FAT32 microSD card (if it uses another format, the device offers to format it).
2. In **Settings → Wi-Fi**, pick the network and type the password (2.4 GHz networks only).
3. **Settings → Browser access** shows the address (by default `http://p4recorder.local`), the
   username (`recorder`) and the password (randomly generated on first boot). Open the address on a
   computer or phone on the same network. Without a router, turn on the **hotspot**
   (`P4Recorder-XXXX`) and open `http://192.168.4.1`.

## Settings on the card

The `CONFIG.JSON` file, in the root of the card, holds every setting, including Wi-Fi passwords.
Edit it on a computer and eject the card: the device applies what changed and shows a notice. Invalid
values are ignored and the file is rewritten with the values in use. The keys are in Portuguese (for
example `"aparelho"`, `"navegador"`) and are kept that way for compatibility. Treat the card as
something that contains passwords.

## Languages

The device starts in English. English and Portuguese are built in; their texts live in
`firmware/lang/en.json` and `firmware/lang/pt.json` (key → text). To add another language, copy one of
them, change `_codigo` (language code) and `_nome` (language name), translate it and put it on the card
as `LANGUAGES/<code>.json` (i.e. `/sdcard/LANGUAGES/<code>.json`). It shows up in **Settings → Language**.
Missing texts fall back to English.

## Publishing a release

```bash
firmware/tools/make_release.sh
```

It builds the firmware and writes to `firmware/release/` the full image (`-full.bin`, for the first
flash), the update file (`-ota.bin`, used by the device's update check), the license package and
`SHA256SUMS.txt`. Attach all of them to a GitHub release whose tag is `v<version>` (the version in
`firmware/src/app.h`). The device only offers a release whose version is newer than its own and that has
an `-ota.bin` file.

## Tests

The plain-C parts (WAV writer, Ogg Opus packaging, MP3, audio filter, `CONFIG.JSON` parsing) have tests
that run on the computer. They need CMake, `libopus`, `ffmpeg` and the ESP-IDF installed by PlatformIO
(cJSON comes from there; another path can be given with `-DCJSON_DIR=...`):

```bash
cmake -S firmware/test_host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Layout

```
firmware/
  src/                 UI (ui.c), core (main.c), web server, settings
  components/          audio, card, display, network, recording formats, languages…
  lang/                English and Portuguese texts
  c6/                  Wi-Fi chip firmware (ESP-Hosted) and how to rebuild it
  test_host/           host-side tests
  tools/ota.sh         firmware upload over Wi-Fi
  tools/make_release.sh  release files for GitHub
docs/                  project documentation
```

More details in [docs/architecture.md](docs/architecture.md) (how the firmware is organized),
[docs/board-validation.md](docs/board-validation.md) (tests run on the board) and
[docs/TODO.md](docs/TODO.md) (next steps).

## License

This project's code is under the MIT license (see [LICENSE](LICENSE)). Third-party components keep
their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
