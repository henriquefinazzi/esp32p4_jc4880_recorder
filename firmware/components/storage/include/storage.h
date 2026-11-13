// microSD of the Guition JC4880P443C: 4-bit SDMMC, powered by internal LDO channel 4.
// The card has one owner at a time: the device (mounted at /sdcard) or the computer,
// when connected over High-Speed USB, which sees it as a USB drive.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STORAGE_MOUNT_POINT "/sdcard"

// Initializes the card, mounts it at /sdcard and enables USB drive mode. Never formats.
esp_err_t storage_mount(void);

// true when the device can read and write (card present and not handed to the computer).
bool storage_is_mounted(void);

// true when the card is handed to the computer over USB.
bool storage_in_use_by_host(void);

// Turns USB presence on or off. Turning it off while recording keeps the computer
// from taking the card in the middle of a file.
void storage_usb_enable(bool enable);


// Total and free space, in MB. false if the card is not with the device.
bool storage_space_mb(uint32_t *total_mb, uint32_t *free_mb);

// Card present but with a file system the device cannot read (exFAT, NTFS...).
// Never formatted on its own: only via storage_format(), after the user confirms.
bool storage_unreadable(void);

// Formats the card as FAT (erases everything) and mounts it. Works for the card in the device
// and for an unrecognized card.
esp_err_t storage_format(void);
