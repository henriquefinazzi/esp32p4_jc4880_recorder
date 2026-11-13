// Recorder Wi-Fi (ESP32-C6 via ESP-Hosted): saved networks, scan, connection and time.
// Only 2.4 GHz networks, with a plain password or open.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define NET_MAX_APS 16
#define NET_MAX_SAVED 4

typedef enum { NET_IDLE, NET_CONNECTING, NET_CONNECTED, NET_FAILED } net_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
    bool saved;
} net_ap_t;

// Turns on the radio and tries the saved networks in the background. Needs NVS already initialized.
esp_err_t net_init(void);

net_state_t net_state(void);

// Network in use or being tried; empty string if none.
void net_current_ssid(char out[33]);

// Background scan. `net_scan_results` returns the total (0 while searching)
// and sets `done` if the scan has finished.
void net_scan_start(void);
int net_scan_results(net_ap_t *out, int max, bool *done);

// Connects without blocking. NULL `password` uses the saved password for that network.
// The network is saved only after a successful connection.
esp_err_t net_connect(const char *ssid, const char *password);

typedef struct {
    char ssid[33];
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
    char mac[18];     // "AA:BB:CC:DD:EE:FF"
    int8_t rssi;      // dBm
    uint8_t channel;
} net_info_t;

// Current connection details. false if not connected.
bool net_info(net_info_t *out);

// Forgets a saved network and, if it is the current one, disconnects.
void net_forget(const char *ssid);

typedef struct {
    char ssid[33];
    char password[65];
} net_saved_t;

// Saved networks, from preferred to least used. Returns how many were filled.
// Works before net_init (only needs NVS).
int net_saved_list(net_saved_t *out, int max);

// Replaces the whole list (up to NET_MAX_SAVED, in the given order). If the network in use
// leaves the list, disconnects.
void net_saved_set(const net_saved_t *list, int n);

// Called whenever the saved network list may have changed (outside the screen task;
// it should only set a flag and return).
void net_on_saved_changed(void (*cb)(void));

// After showing a failure to the user: moves NET_FAILED back to NET_IDLE.
void net_ack_failure(void);

// true after the clock has been set from the internet.
bool net_time_valid(void);

// Sets the clock from an external time (e.g. the browser), only if the time has not
// yet come from the NTP server. true if it was set.
bool net_set_time_from(long long epoch_s);

// Time server (NTP). May be called before net_init; if the time is already being
// fetched, switches servers immediately.
void net_set_ntp_server(const char *server);

// When (time_t) the time last came from the server; 0 if it has not yet.
long long net_last_time_sync(void);

// Recorder's own network (access point). When on, the recorder leaves the Wi-Fi network it
// was on (no scan, no connection, no internet time) and returns to it when turned off.
// May be called before net_init. `pass` with 8 to 63 characters (WPA2).
void net_ap_set(bool on, const char *ssid, const char *pass);

typedef struct {
    bool on;          // requested and on the air
    char ip[16];      // recorder's address on this network
    int clients;      // connected devices
} net_ap_info_t;

void net_ap_info(net_ap_info_t *out);

// ---------- Wi-Fi chip (ESP32-C6, ESP-Hosted) ----------

// Version of the firmware on the C6 ("2.3.0"). false if the C6 did not respond.
bool net_wifi_fw_version(char out[24]);

// Version of the C6 firmware bundled in this firmware ("2.12.13").
const char *net_wifi_fw_bundled(void);

// Starts updating the C6 with the embedded firmware, in the background. At the end the device
// restarts. false if an update is already running or the C6 is not ready.
bool net_wifi_fw_update_start(void);

// -1 idle; 0 to 100 sending; -2 failed (the C6 keeps the previous version).
int net_wifi_fw_update_progress(void);
