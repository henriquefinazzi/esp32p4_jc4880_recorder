// Boundary between the recorder core (main.c) and the user interface (ui.c).
// The interface only reads state and requests actions through here.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define APP_VERSION "0.5"
#define APP_GAIN_MIN_DB 0
#define APP_GAIN_MAX_DB 42
#define APP_GAIN_STEP_DB 6 // ES8311 preamp steps

typedef enum {
    APP_EVENT_NONE,
    APP_EVENT_SAVED,      // recording finished and was saved
    APP_EVENT_NO_CARD,    // tried to record without a card
    APP_EVENT_CARD_BUSY,  // tried to record with the card on the computer
    APP_EVENT_CARD_FULL,  // ran out of space while recording (what was there was saved)
    APP_EVENT_WRITE_ERROR // other write error (what was there was saved)
} app_event_t;

typedef struct {
    bool recording;
    uint32_t rec_seconds;
    uint32_t rec_bytes;
    char rec_name[16];   // "REC_0023"
    float level_dbfs;    // peak over the last 100 ms
    int gain_db;
    bool updating;       // receiving new firmware over Wi-Fi

    bool card_ok;        // card present and held by the device
    bool card_on_host;   // card handed to the computer over USB
    bool card_unreadable; // card present in a format the device can't read (exFAT, NTFS...)
    uint32_t total_mb;
    uint32_t free_mb;

    uint32_t event_seq;  // changes with each new event
    app_event_t event;
    uint32_t event_seconds; // saved duration, for SAVED / CARD_FULL / WRITE_ERROR
} app_status_t;

#define APP_TITLE_MAX 60 // bytes of the user-given name (UTF-8)

// A recording. The file is "REC_0023.WAV" or, with a name, "REC_0023 Client meeting.WAV":
// the name travels with the file (USB drive, download). The number identifies the recording.
typedef struct {
    int id;                        // 23
    char file[96];                 // file name on the card
    char title[APP_TITLE_MAX + 1]; // "" if it has no name
    uint32_t bytes;
    uint32_t seconds;
    time_t mtime;                  // 0 if the clock was not set when recording
} app_recording_t;

void app_get_status(app_status_t *out);

void app_toggle_recording(void);

// Firmware update in progress (from the browser): prevents starting a recording.
void app_set_updating(bool updating);
void app_set_gain_db(int db);            // clamps to the range and saves as default

int app_screen_timeout_s(void);          // 0 = never turn off
void app_set_screen_timeout_s(int seconds);

// Interface language (code: pt, en or a LANGUAGES/<code>.json file on the card).
#define APP_LANG_MAX 15
void app_language(char out[APP_LANG_MAX + 1]);
void app_set_language(const char *code); // the screen switches on the next cycle

// Format of the next recordings: 0 WAV, 1 Opus, 2 MP3 (see rec_writer.h).
int app_rec_format(void);
void app_set_rec_format(int fmt);
// Bytes per second of recording in the chosen format (to estimate the time that fits).
uint32_t app_rec_bytes_per_s(void);

// Low-frequency noise filter on the recorded audio.
bool app_filter_enabled(void);
void app_set_filter_enabled(bool on);

// Card full: delete the oldest recordings to keep recording.
#define APP_AUTODELETE_MIN_MB 300 // free space kept while recording
bool app_autodelete_enabled(void);
void app_set_autodelete_enabled(bool on);

// Screen lock with a numeric code (4 to 8 digits). Turns on only if a code is set.
#define APP_LOCK_PIN_MAX 8
bool app_lock_enabled(void);
bool app_set_lock_enabled(bool on);          // false if no code is set
void app_lock_pin(char out[APP_LOCK_PIN_MAX + 1]);
bool app_set_lock_pin(const char *pin);      // false if invalid
int app_lock_after_s(void);                  // inactivity before locking: 60, 300 or 900
void app_set_lock_after_s(int seconds);

// Screen theme: 0 dark, 1 light.
int app_theme(void);
void app_set_theme(int theme);

int app_brightness(void);                // percent
void app_set_brightness(int percent, bool save); // save=false while the finger drags the slider

// Newest first. Returns how many were filled.
int app_list_recordings(app_recording_t *out, int max);
bool app_delete_recording(int id);
// Sets or changes the name ("" removes it). Symbols the card rejects in names are dropped.
bool app_rename_recording(int id, const char *title);
// Full file path of a recording. false if it does not exist.
bool app_recording_path(int id, char *out, size_t len);
bool app_format_card(void);
bool app_retry_card(void);

// Username and password for browser access. On first boot: username "recorder" and a
// random 6-digit password. Printable ASCII only; the username cannot contain ":".
#define APP_WEB_USER_MAX 32
#define APP_WEB_PASS_MIN 4
#define APP_WEB_PASS_MAX 32
void app_web_user(char out[APP_WEB_USER_MAX + 1]);
void app_web_pass(char out[APP_WEB_PASS_MAX + 1]);
bool app_set_web_user(const char *user); // false if invalid
bool app_set_web_pass(const char *pass); // false if invalid

// Device name on the local network (http://<name>.local). Lowercase letters, digits and
// hyphens only, no hyphen at either end, 1 to APP_HOSTNAME_MAX characters.
#define APP_HOSTNAME_MAX 24
void app_hostname(char out[APP_HOSTNAME_MAX + 1]);
bool app_set_hostname(const char *name); // false if the name is invalid

// Time server (NTP), e.g. pool.ntp.org or a.st1.ntp.br.
#define APP_NTP_MAX 63
void app_ntp_server(char out[APP_NTP_MAX + 1]);
bool app_set_ntp_server(const char *server); // false if invalid

// Recorder's own network (access point). Name and password are randomized the first time.
#define APP_AP_SSID_MAX 32
#define APP_AP_PASS_MAX 63
bool app_ap_enabled(void);
void app_set_ap_enabled(bool on);
void app_ap_ssid(char out[APP_AP_SSID_MAX + 1]);
void app_ap_pass(char out[APP_AP_PASS_MAX + 1]);
bool app_set_ap_ssid(const char *ssid); // false if invalid
bool app_set_ap_pass(const char *pass); // false if invalid (8 to 63 characters)

// Developer features over the network: update firmware, read the log, capture the screen.
// When off, these endpoints respond that they are disabled.
bool app_dev_enabled(void);
void app_set_dev_enabled(bool on);

// Turns browser access on or off. The choice is saved.
bool app_web_enabled(void);
void app_set_web_enabled(bool enabled);
