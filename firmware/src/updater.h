// Updates from the project's GitHub releases.
// Checks the latest release; if its version is newer than this firmware and it carries an
// "*-ota.bin" asset, downloads it over HTTPS (certificates checked against the built-in
// bundle), writes it to the free firmware slot and restarts. The usual rollback applies:
// if the new firmware does not finish starting up, the next restart goes back.
#pragma once

#include <stdbool.h>

#define UPDATER_REPO "henriquefinazzi/esp32p4_jc4880_recorder" // owner/name on GitHub

typedef enum {
    UPD_IDLE,
    UPD_CHECKING,
    UPD_UP_TO_DATE,
    UPD_AVAILABLE,
    UPD_PREPARING, // erasing the firmware slot (screen dark)
    UPD_DOWNLOADING,
    UPD_DONE,      // written; the device restarts in a moment
    UPD_ERROR,
} upd_state_t;

// Starts a check in the background. false if a check or download is already running.
bool updater_check_start(void);

// Downloads and installs the version found by the last check. false if there is none.
bool updater_install_start(void);

upd_state_t updater_state(void);
const char *updater_latest(void);   // version of the latest release ("0.6"), or ""
int updater_progress(void);         // 0 to 100 while downloading
const char *updater_error_key(void); // i18n key describing the last error
