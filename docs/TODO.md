# TODO

Planned for after version 1.0.

- [ ] **Battery:** measure consumption (recording with the screen off), show the charge in the top bar and
  close the recording safely when the battery is nearly empty. First: check in the schematic whether the
  battery voltage reaches a readable pin (ADC).
- [ ] **Search by recording name:** in the list on the device and on the web page.
- [ ] **Meeting markers:** a tap during recording marks the moment; the markers show up in the list and on
  the web page.
- [ ] **Screen-off shortcut on the home screen:** a button to turn the screen off right away.
- [ ] **Screen-off shortcut on the lock screen:** the same shortcut next to the code keypad.
- [ ] **Bigger numeric keypad:** larger keys on the lock screen and on the code setup screen.
- [ ] **Direct updates from the web:** check the GitHub repository for a new release and install it
  from the device, without downloading the firmware by hand.
- [ ] **Recording format on the home screen:** show the selected file format (WAV, Opus or MP3).
- [ ] **Wi-Fi information on the home screen:** network name and signal (or the device's own network).
- [ ] **Animated Wi-Fi rescan button:** spin the button while the scan is running.

Done:

- [x] ESP32-C6 firmware update from the device (Settings > About).
- [x] Browser access lockout after several wrong passwords.
- [x] Screen lock with a numeric code.
