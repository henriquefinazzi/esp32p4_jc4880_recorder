// Mirror of the settings on the card: /sdcard/CONFIG.JSON.
// Internal memory (NVS) remains the source: without a card the device boots configured.
// - Every change made on the device rewrites the file (after a short delay).
// - Whenever the card returns to the device (at boot or after USB drive mode), the
//   file is read: valid values that changed are applied; invalid ones are ignored.
// - A file with a syntax error is left untouched until the next change made on the device.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// After the card, audio and display; before Wi-Fi and the web server.
void config_init(void);

// Signals that some setting changed on the device.
void config_changed(void);

// Kind of notice about the file read; the screen builds the text in the chosen language.
typedef enum {
    CONFIG_NOTICE_NONE,
    CONFIG_NOTICE_APPLIED,         // changes applied
    CONFIG_NOTICE_APPLIED_IGNORED, // changes applied, with invalid values ignored
    CONFIG_NOTICE_IGNORED,         // nothing changed; only invalid values ignored
    CONFIG_NOTICE_UNREADABLE,      // file with a syntax error, ignored entirely
} config_notice_t;

// Pending notice for the screen (file applied or with errors) and how many values were
// ignored. false if there is none.
bool config_take_notice(config_notice_t *kind, int *ignored);
