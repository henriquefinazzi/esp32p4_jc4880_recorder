# Local copy of espressif/esp_tinyusb 1.7.6~2

Copied from managed_components to fix a data-loss bug: when mounting a card whose file
system FATFS does not recognize (exFAT, NTFS, corrupted), `_mount()` formatted the card
on its own, ignoring `mount_config.format_if_mount_failed`.

Change (search for "P4 Recorder:" in tusb_msc_storage.c): the field is stored in the handle and
`_mount()` only formats if it is true. The P4 Recorder firmware always uses false.

When updating esp_tinyusb, check whether Espressif already honors the field; if not, reapply the patch.

Second change: `tinyusb_msc_storage_unformatted()` reports whether the last mount failed because there
was no file system (e.g. a card formatted as exFAT on a computer and then ejected).
