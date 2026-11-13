// Recorder settings in JSON (CONFIG.JSON file on the card).
// Plain C, nothing from ESP-IDF besides cJSON: tested on the computer (test_host).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SETTINGS_VERSION 1

#define SETTINGS_HOST_MAX 24
#define SETTINGS_USER_MAX 32
#define SETTINGS_PASS_MIN 4
#define SETTINGS_PASS_MAX 32
#define SETTINGS_WIFI_MAX 4
#define SETTINGS_SSID_MAX 32
#define SETTINGS_WIFI_PASS_MAX 64
#define SETTINGS_NTP_MAX 63
#define SETTINGS_LANG_MAX 15
#define SETTINGS_LOCK_PIN_MIN 4
#define SETTINGS_LOCK_PIN_MAX 8
#define SETTINGS_AP_PASS_MIN 8
#define SETTINGS_AP_PASS_MAX 63

#define SETTINGS_BRIGHTNESS_MIN 10
#define SETTINGS_GAIN_MIN_DB 0
#define SETTINGS_GAIN_MAX_DB 42
#define SETTINGS_GAIN_STEP_DB 6

typedef struct {
    char ssid[SETTINGS_SSID_MAX + 1];
    char pass[SETTINGS_WIFI_PASS_MAX + 1];
} settings_wifi_t;

typedef struct {
    int brightness;   // %
    int screen_off_s; // 0, 10, 30 or 60 (0 = never)
    int gain_db;
    int theme;        // 0 dark, 1 light
    int rec_format;   // 0 wav, 1 opus, 2 mp3
    bool filter;      // low-frequency noise filter
    bool autodelete;  // card full: delete the oldest recordings
    bool lock_on;     // screen lock with code
    char lock_pin[SETTINGS_LOCK_PIN_MAX + 1];
    int lock_after_s; // 60, 300 or 900
    char lang[SETTINGS_LANG_MAX + 1]; // language code: pt, en or a file on the card
    bool web_on;
    bool dev_on;      // developer functions via web (firmware, log, screenshot)
    char host[SETTINGS_HOST_MAX + 1];
    char user[SETTINGS_USER_MAX + 1];
    char pass[SETTINGS_PASS_MAX + 1];
    int wifi_count;   // in order of preference
    settings_wifi_t wifi[SETTINGS_WIFI_MAX];
    char ntp[SETTINGS_NTP_MAX + 1];           // time server
    bool ap_on;                               // recorder's own network
    char ap_ssid[SETTINGS_SSID_MAX + 1];
    char ap_pass[SETTINGS_AP_PASS_MAX + 1];
} settings_t;

// Rules for each value (the same ones the screen uses).
bool settings_host_valid(const char *s);
bool settings_user_valid(const char *s);
bool settings_pass_valid(const char *s);
bool settings_wifi_valid(const char *ssid, const char *pass);
bool settings_lang_valid(const char *s);   // language code: 2 to 15 lowercase letters, digits or "-"
bool settings_ntp_valid(const char *s);      // name or IP: letters, digits, dot and hyphen
bool settings_lock_pin_valid(const char *s); // 4 to 8 digits
bool settings_ap_ssid_valid(const char *s); // 1 to 32 bytes, no control characters
bool settings_ap_pass_valid(const char *s); // WPA2: 8 to 63 printable ASCII characters

// Indented JSON text. Returns malloc'd memory (release with free) or NULL.
char *settings_to_json(const settings_t *s);

// Reads `text` on top of `current`: anything missing or invalid stays as in
// `current`, and each invalid value counts in `*ignored`. false if the text is not JSON
// with an object at the root (then `out` receives `current` unchanged).
bool settings_from_json(const char *text, const settings_t *current, settings_t *out, int *ignored);

// true if both settings are equal.
bool settings_equal(const settings_t *a, const settings_t *b);
