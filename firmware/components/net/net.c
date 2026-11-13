#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define NVS_NAMESPACE "net"
#define TIMEZONE "<-03>3" // Brasília time
#define TIME_VALID_AFTER 1700000000
#define CONNECT_TIMEOUT_MS 15000
#define AUTOCONNECT_PERIOD_MS 15000
#define AP_MAX_CLIENTS 4

static const char *TAG = "net";

static bool s_ready;
static esp_netif_t *s_netif;
static volatile net_state_t s_state = NET_IDLE;
static char s_ssid[33];
static char s_pending_pass[65];
static bool s_sntp_started;
static TickType_t s_connect_started;
static volatile bool s_auto_attempt; // the current attempt is automatic, not requested on the screen

static net_ap_t s_aps[NET_MAX_APS];
static void (*s_saved_changed)(void);
static char s_ntp[64] = "pool.ntp.org";
static volatile long long s_last_sync;
static esp_netif_t *s_ap_netif;
static bool s_ap_on;
static char s_ap_ssid[33], s_ap_pass[64];
static volatile int s_ap_clients;
static volatile int s_ap_count;
static volatile bool s_scan_done = true;
static volatile bool s_scan_running;

static void key(char *out, char prefix, int i)
{
    out[0] = prefix;
    out[1] = (char)('0' + i);
    out[2] = 0;
}

static bool saved_get(int i, char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    char k[3];
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    key(k, 's', i);
    bool ok = nvs_get_str(h, k, ssid, &ssid_len) == ESP_OK && ssid[0];
    if (ok && pass) {
        key(k, 'p', i);
        if (nvs_get_str(h, k, pass, &pass_len) != ESP_OK) pass[0] = 0;
    }
    nvs_close(h);
    return ok;
}

static int saved_find(const char *ssid, char *pass, size_t pass_len)
{
    for (int i = 0; i < NET_MAX_SAVED; i++) {
        char s[33];
        if (saved_get(i, s, sizeof(s), pass, pass_len) && strcmp(s, ssid) == 0) return i;
    }
    return -1;
}

static void saved_write(char ss[][33], char pp[][65], int n)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    char k[3];
    for (int i = 0; i < NET_MAX_SAVED; i++) {
        key(k, 's', i);
        nvs_set_str(h, k, i < n ? ss[i] : "");
        key(k, 'p', i);
        nvs_set_str(h, k, i < n ? pp[i] : "");
    }
    nvs_commit(h);
    nvs_close(h);
    if (s_saved_changed) s_saved_changed();
}

// Moves the network to the front of the list (the most recent is tried first at boot).
static void saved_put(const char *ssid, const char *pass)
{
    char ss[NET_MAX_SAVED][33] = {0};
    char pp[NET_MAX_SAVED][65] = {0};
    int n = 0;
    for (int i = 0; i < NET_MAX_SAVED && n < NET_MAX_SAVED - 1; i++) {
        char s[33], p[65];
        if (saved_get(i, s, sizeof(s), p, sizeof(p)) && strcmp(s, ssid) != 0) {
            strlcpy(ss[n], s, sizeof(ss[n]));
            strlcpy(pp[n], p, sizeof(pp[n]));
            n++;
        }
    }
    // Nothing changed (reconnection to the same network, already in front): do not rewrite.
    char s0[33], p0[65];
    if (saved_get(0, s0, sizeof(s0), p0, sizeof(p0)) && strcmp(s0, ssid) == 0 && strcmp(p0, pass) == 0) return;
    for (int i = n; i > 0; i--) {
        memcpy(ss[i], ss[i - 1], sizeof(ss[i]));
        memcpy(pp[i], pp[i - 1], sizeof(pp[i]));
    }
    strlcpy(ss[0], ssid, sizeof(ss[0]));
    strlcpy(pp[0], pass, sizeof(pp[0]));
    saved_write(ss, pp, n + 1);
}

static void on_time_sync(struct timeval *tv)
{
    s_last_sync = tv->tv_sec;
}

static void sntp_start(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp);
    cfg.sync_cb = on_time_sync;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntp_started = true;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_ap_clients++;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_ap_clients > 0) s_ap_clients--;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == NET_CONNECTING) {
            s_state = NET_FAILED;
            esp_wifi_disconnect(); // stops the internal retries, which would block the scan
        } else if (s_state == NET_CONNECTED) {
            // Dropped: one more attempt on the same network; if it fails, stays in NET_FAILED.
            s_state = NET_CONNECTING;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_state = NET_CONNECTED;
        saved_put(s_ssid, s_pending_pass);
        if (!s_sntp_started) sntp_start();
    }
}

static esp_err_t connect_to(const char *ssid, const char *password);

// Keeps the device connected: at power-on and whenever it has no network, tries the saved
// networks, from most recent to oldest, and repeats every AUTOCONNECT_PERIOD_MS.
static void autoconnect_task(void *arg)
{
    for (;;) {
        if (!s_ap_on && s_state != NET_CONNECTED && s_state != NET_CONNECTING && !s_scan_running) {
            for (int i = 0; i < NET_MAX_SAVED; i++) {
                char ssid[33];
                if (!saved_get(i, ssid, sizeof(ssid), NULL, 0)) continue;
                s_auto_attempt = true;
                if (connect_to(ssid, NULL) != ESP_OK) continue;
                for (int w = 0; w < 120 && s_state == NET_CONNECTING && s_auto_attempt; w++) vTaskDelay(pdMS_TO_TICKS(100));
                if (!s_auto_attempt) break; // another network was chosen on the screen meanwhile: it takes priority
                if (s_state == NET_CONNECTED) break;
                if (net_state() == NET_FAILED) s_state = NET_IDLE; // an automatic failure does not become a warning on screen
            }
            s_auto_attempt = false;
        }
        vTaskDelay(pdMS_TO_TICKS(AUTOCONNECT_PERIOD_MS));
    }
}

// Turns the access point on or off according to s_ap_on (only after the radio is up).
// Own network on = access point only: the recorder leaves the other network until it is turned off.
static void ap_apply(void)
{
    if (!s_ready) return;
    if (!s_ap_on) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        s_ap_clients = 0;
        s_state = NET_IDLE; // the reconnect task goes back to the saved networks on its next round
        return;
    }
    s_auto_attempt = false;
    s_state = NET_IDLE; // a requested disconnect is neither a failure nor a drop
    esp_wifi_disconnect();
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)ap.ap.password, s_ap_pass, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = AP_MAX_CLIENTS;
    ap.ap.channel = 6;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) ESP_LOGW(TAG, "own network: %s", esp_err_to_name(err));
}

esp_err_t net_init(void)
{
    if (s_ready) return ESP_OK;

    setenv("TZ", TIMEZONE, 1);
    tzset();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_netif = esp_netif_create_default_wifi_sta();

    // ESP-Hosted does not start on its own in this project: init it and connect to the C6 before Wi-Fi.
    if (esp_hosted_init() != 0) return ESP_FAIL;
    if (esp_hosted_connect_to_slave() != 0) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;

    s_ready = true;
    ap_apply();
    xTaskCreate(autoconnect_task, "net_auto", 4096, NULL, 3, NULL);
    return ESP_OK;
}

net_state_t net_state(void)
{
    // A wrong password sometimes produces no disconnect event: give up on timeout.
    if (s_state == NET_CONNECTING && xTaskGetTickCount() - s_connect_started > pdMS_TO_TICKS(CONNECT_TIMEOUT_MS)) {
        s_state = NET_FAILED;
        esp_wifi_disconnect();
    }
    return s_state;
}

void net_ack_failure(void)
{
    if (s_state == NET_FAILED) s_state = NET_IDLE;
}

void net_current_ssid(char out[33])
{
    strlcpy(out, s_state == NET_IDLE ? "" : s_ssid, 33);
}

static void scan_task(void *arg)
{
    static wifi_ap_record_t recs[NET_MAX_APS * 2];
    uint16_t n = NET_MAX_APS * 2;
    int count = 0;
    ESP_LOGI(TAG, "scan: starting");
    esp_err_t scan_err = esp_wifi_scan_start(NULL, true);
    if (scan_err != ESP_OK && s_state != NET_CONNECTED && s_state != NET_CONNECTING) {
        // The radio may be stuck in an old connection attempt: release it and try again.
        ESP_LOGW(TAG, "scan: %s, retrying", esp_err_to_name(scan_err));
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        scan_err = esp_wifi_scan_start(NULL, true);
    }
    if (scan_err == ESP_OK && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
        for (int i = 0; i < n && count < NET_MAX_APS; i++) {
            const char *ssid = (const char *)recs[i].ssid;
            if (!ssid[0]) continue; // hidden network
            bool dup = false;
            for (int j = 0; j < count; j++) dup = dup || strcmp(s_aps[j].ssid, ssid) == 0;
            if (dup) continue;
            strlcpy(s_aps[count].ssid, ssid, sizeof(s_aps[count].ssid));
            s_aps[count].rssi = recs[i].rssi;
            s_aps[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
            s_aps[count].saved = saved_find(ssid, NULL, 0) >= 0;
            count++;
        }
    } else {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(scan_err));
    }
    ESP_LOGI(TAG, "scan: %d networks", count);
    s_ap_count = count;
    s_scan_done = true;
    s_scan_running = false;
    vTaskDelete(NULL);
}

void net_scan_start(void)
{
    if (!s_ready || s_scan_running || s_ap_on) return; // access point only: no scan
    s_scan_running = true;
    s_scan_done = false;
    s_ap_count = 0;
    xTaskCreate(scan_task, "net_scan", 6144, NULL, 3, NULL);
}

int net_scan_results(net_ap_t *out, int max, bool *done)
{
    if (done) *done = s_scan_done;
    if (!s_scan_done) return 0;
    int n = s_ap_count < max ? s_ap_count : max;
    memcpy(out, s_aps, (size_t)n * sizeof(net_ap_t));
    return n;
}

esp_err_t net_connect(const char *ssid, const char *password)
{
    if (s_ap_on) return ESP_ERR_INVALID_STATE; // the own network occupies the radio
    s_auto_attempt = false; // a request from the screen takes priority over the automatic attempt
    return connect_to(ssid, password);
}

static esp_err_t connect_to(const char *ssid, const char *password)
{
    if (!s_ready || !ssid || !ssid[0]) return ESP_ERR_INVALID_STATE;

    char pass[65] = {0};
    if (password) {
        strlcpy(pass, password, sizeof(pass));
    } else if (saved_find(ssid, pass, sizeof(pass)) < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));

    s_state = NET_IDLE; // the disconnect below must not count as a failure
    esp_wifi_disconnect();
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pending_pass, pass, sizeof(s_pending_pass));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;
    s_connect_started = xTaskGetTickCount();
    s_state = NET_CONNECTING;
    err = esp_wifi_connect();
    if (err != ESP_OK) s_state = NET_FAILED;
    return err;
}

bool net_info(net_info_t *out)
{
    if (!out || s_state != NET_CONNECTED || !s_netif) return false;
    memset(out, 0, sizeof(*out));
    strlcpy(out->ssid, s_ssid, sizeof(out->ssid));

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) {
        esp_ip4addr_ntoa(&ip.ip, out->ip, sizeof(out->ip));
        esp_ip4addr_ntoa(&ip.netmask, out->mask, sizeof(out->mask));
        esp_ip4addr_ntoa(&ip.gw, out->gateway, sizeof(out->gateway));
    }
    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, out->dns, sizeof(out->dns));
    }
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(out->mac, sizeof(out->mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
        out->channel = ap.primary;
    }
    return true;
}

void net_forget(const char *ssid)
{
    if (!ssid || !ssid[0]) return;
    char ss[NET_MAX_SAVED][33] = {0};
    char pp[NET_MAX_SAVED][65] = {0};
    int n = 0;
    for (int i = 0; i < NET_MAX_SAVED; i++) {
        char s[33], p[65];
        if (saved_get(i, s, sizeof(s), p, sizeof(p)) && strcmp(s, ssid) != 0) {
            strlcpy(ss[n], s, sizeof(ss[n]));
            strlcpy(pp[n], p, sizeof(pp[n]));
            n++;
        }
    }
    saved_write(ss, pp, n);
    if (strcmp(s_ssid, ssid) == 0 && s_state != NET_IDLE) {
        s_state = NET_IDLE; // a requested disconnect is neither a failure nor a drop
        esp_wifi_disconnect();
    }
}

int net_saved_list(net_saved_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < NET_MAX_SAVED && n < max; i++) {
        if (saved_get(i, out[n].ssid, sizeof(out[n].ssid), out[n].password, sizeof(out[n].password))) n++;
    }
    return n;
}

void net_saved_set(const net_saved_t *list, int n)
{
    char ss[NET_MAX_SAVED][33] = {0};
    char pp[NET_MAX_SAVED][65] = {0};
    if (n > NET_MAX_SAVED) n = NET_MAX_SAVED;
    bool current_kept = false;
    for (int i = 0; i < n; i++) {
        strlcpy(ss[i], list[i].ssid, sizeof(ss[i]));
        strlcpy(pp[i], list[i].password, sizeof(pp[i]));
        current_kept |= strcmp(ss[i], s_ssid) == 0;
    }
    saved_write(ss, pp, n);
    if (s_ready && s_ssid[0] && !current_kept && s_state != NET_IDLE) {
        s_state = NET_IDLE; // a requested disconnect is neither a failure nor a drop
        esp_wifi_disconnect();
    }
}

void net_set_ntp_server(const char *server)
{
    if (!server || !server[0] || strcmp(server, s_ntp) == 0) return;
    strlcpy(s_ntp, server, sizeof(s_ntp));
    if (s_sntp_started) {
        esp_netif_sntp_deinit();
        s_sntp_started = false;
        s_last_sync = 0;
        sntp_start();
    }
}

bool net_set_time_from(long long epoch_s)
{
    if (s_last_sync || epoch_s < TIME_VALID_AFTER) return false; // NTP wins; absurd times are rejected
    time_t now = time(NULL);
    if (now > TIME_VALID_AFTER && llabs((long long)now - epoch_s) < 30) return false; // already correct
    struct timeval tv = {.tv_sec = (time_t)epoch_s};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "clock set by the browser");
    return true;
}

long long net_last_time_sync(void)
{
    return s_last_sync;
}

void net_ap_set(bool on, const char *ssid, const char *pass)
{
    s_ap_on = on;
    if (ssid) strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    if (pass) strlcpy(s_ap_pass, pass, sizeof(s_ap_pass));
    ap_apply();
}

void net_ap_info(net_ap_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->on = s_ap_on && s_ready && s_ap_netif;
    out->clients = s_ap_clients;
    esp_netif_ip_info_t ip;
    if (out->on && esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&ip.ip));
}

// ---------- Wi-Fi chip update ----------

extern const uint8_t c6_fw_start[] asm("_binary_network_adapter_esp32c6_bin_start");
extern const uint8_t c6_fw_end[] asm("_binary_network_adapter_esp32c6_bin_end");
static volatile int s_c6_progress = -1;

bool net_wifi_fw_version(char out[24])
{
    out[0] = 0;
    if (!s_ready) return false;
    esp_hosted_coprocessor_fwver_t v = {0};
    if (esp_hosted_get_coprocessor_fwversion(&v) != ESP_OK) return false;
    snprintf(out, 24, "%lu.%lu.%lu", (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
    return true;
}

const char *net_wifi_fw_bundled(void)
{
    // The app descriptor sits right after the image header and the first segment.
    const esp_app_desc_t *d = (const esp_app_desc_t *)(c6_fw_start + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    return d->magic_word == ESP_APP_DESC_MAGIC_WORD ? d->version : "?";
}

int net_wifi_fw_update_progress(void)
{
    return s_c6_progress;
}

static void c6_update_task(void *arg)
{
    const size_t total = (size_t)(c6_fw_end - c6_fw_start);
    char before[24];
    net_wifi_fw_version(before);
    ESP_LOGI(TAG, "wi-fi chip: updating %s -> %s (%u bytes)", before, net_wifi_fw_bundled(), (unsigned)total);
    esp_err_t err = esp_hosted_slave_ota_begin();
    for (size_t off = 0; err == ESP_OK && off < total; off += 1400) {
        size_t n = total - off < 1400 ? total - off : 1400;
        err = esp_hosted_slave_ota_write((uint8_t *)(c6_fw_start + off), (uint32_t)n);
        s_c6_progress = (int)((off + n) * 100 / total);
    }
    if (err == ESP_OK) err = esp_hosted_slave_ota_end();
    // From 2.6 on the C6 needs an explicit activation; earlier versions switch on their own.
    if (err == ESP_OK && strcmp(before, "2.6.0") >= 0 && strlen(before) >= 5) esp_hosted_slave_ota_activate();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wi-fi chip: update failed (%s)", esp_err_to_name(err));
        s_c6_progress = -2;
        vTaskDelete(NULL);
        return;
    }
    s_c6_progress = 100;
    ESP_LOGI(TAG, "wi-fi chip: updated, restarting");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart(); // on boot, the P4 resets the C6 (GPIO 54), which comes up with the new version
}

bool net_wifi_fw_update_start(void)
{
    if (!s_ready || (s_c6_progress >= 0 && s_c6_progress < 100)) return false;
    s_c6_progress = 0;
    return xTaskCreate(c6_update_task, "c6_ota", 6144, NULL, 4, NULL) == pdPASS;
}

void net_on_saved_changed(void (*cb)(void))
{
    s_saved_changed = cb;
}

bool net_time_valid(void)
{
    return time(NULL) > TIME_VALID_AFTER;
}
