// Recorder core: audio capture, WAV recording to microSD, settings.
//
// Two tasks handle audio: `audio` only reads the codec and never waits for the card;
// `recorder` is the sole owner of the file. Between them sits a buffer of a few seconds,
// because a microSD write can stall for hundreds of milliseconds.
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app.h"
#include "audio_in.h"
#include "config.h"
#include "i18n.h"
#include "logbuf.h"
#include "display.h"
#include "net.h"
#include "settings_file.h"
#include "storage.h"
#include "ui.h"
#include "audio_dsp.h"
#include "rec_writer.h"
#include "web.h"

#define PIN_BOOT GPIO_NUM_35

#define SAMPLE_RATE_HZ 16000
#define BLOCK_SAMPLES 1600            // 100 ms
#define BLOCK_BYTES (BLOCK_SAMPLES * sizeof(int16_t))
#define BLOCKS_PER_SECOND 10
#define CHECKPOINT_BLOCKS 20          // 2 s: a power loss loses at most this much
#define BUFFER_SECONDS 8              // headroom for card stalls
#define BUFFER_BYTES (BUFFER_SECONDS * BLOCKS_PER_SECOND * BLOCK_BYTES)
#define DEBOUNCE_MS 50
#define MIN_LISTED_BYTES 1024         // ignores leftovers of recordings that never got any audio

#define NVS_NAMESPACE "cfg"
#define DEFAULT_GAIN_DB 30
#define DEFAULT_SCREEN_TIMEOUT_S 10

static const char *TAG = "recorder";

static volatile bool s_toggle_requested;
static volatile bool s_updating;              // receiving firmware over Wi-Fi
static volatile bool s_recording;          // the audio task feeds the buffer when true
static volatile uint32_t s_dropped_blocks; // blocks dropped because the buffer was full
static StreamBufferHandle_t s_stream;

// State read by the interface.
static volatile float s_level_dbfs = -96.0f;
static volatile uint32_t s_rec_seconds;
static volatile uint32_t s_rec_bytes;
static char s_rec_name[16];
static volatile uint32_t s_total_mb;
static volatile uint32_t s_free_mb;
static volatile uint32_t s_event_seq;
static volatile app_event_t s_event;
static volatile uint32_t s_event_seconds;
static volatile int s_gain_db = DEFAULT_GAIN_DB;
static volatile int s_screen_timeout_s = DEFAULT_SCREEN_TIMEOUT_S;
static volatile int s_brightness = 100;
static volatile int s_theme;
static volatile int s_rec_format = REC_FMT_MP3;
static volatile bool s_filter = true;
static volatile bool s_autodelete;
static volatile bool s_lock_on;
static char s_lock_pin[APP_LOCK_PIN_MAX + 1];
static volatile int s_lock_after_s = 300;
static char s_lang[APP_LANG_MAX + 1] = "en"; // a new device starts in English
static char s_web_user[APP_WEB_USER_MAX + 1] = "recorder";
static char s_web_pass[APP_WEB_PASS_MAX + 1];
static char s_hostname[APP_HOSTNAME_MAX + 1] = "p4recorder";
static char s_ntp[APP_NTP_MAX + 1] = "pool.ntp.org";
static bool s_ap_on;
static volatile bool s_dev_on = true;
static char s_ap_ssid[APP_AP_SSID_MAX + 1];
static char s_ap_pass[APP_AP_PASS_MAX + 1];
static volatile bool s_web_enabled = true;
static bool s_net_ready;

// ---------- Settings ----------

static void cfg_set_str(const char *key, const char *value);

static int cfg_get(const char *key, int fallback)
{
    nvs_handle_t h;
    int32_t v = fallback;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, key, &v);
        nvs_close(h);
    }
    return (int)v;
}

static void cfg_set(const char *key, int value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, key, value);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---------- API for the interface ----------

static void raise_event(app_event_t event, uint32_t seconds)
{
    s_event = event;
    s_event_seconds = seconds;
    s_event_seq++;
}

static void refresh_space(void)
{
    uint32_t total = 0, free_mb = 0;
    if (storage_space_mb(&total, &free_mb)) {
        s_total_mb = total;
        s_free_mb = free_mb;
    }
}

void app_get_status(app_status_t *out)
{
    out->recording = s_recording;
    out->rec_seconds = s_rec_seconds;
    out->rec_bytes = s_rec_bytes;
    strlcpy(out->rec_name, s_rec_name, sizeof(out->rec_name));
    out->level_dbfs = s_level_dbfs;
    out->gain_db = s_gain_db;
    out->updating = s_updating;
    out->card_ok = storage_is_mounted();
    out->card_on_host = storage_in_use_by_host();
    out->card_unreadable = storage_unreadable();
    out->total_mb = s_total_mb;
    out->free_mb = s_free_mb;
    out->event_seq = s_event_seq;
    out->event = s_event;
    out->event_seconds = s_event_seconds;
}

void app_toggle_recording(void)
{
    if (s_updating && !s_recording) return; // the device will restart with the new firmware
    s_toggle_requested = true;
}

void app_set_updating(bool updating)
{
    s_updating = updating;
}

void app_set_gain_db(int db)
{
    if (db < APP_GAIN_MIN_DB) db = APP_GAIN_MIN_DB;
    if (db > APP_GAIN_MAX_DB) db = APP_GAIN_MAX_DB;
    db -= db % APP_GAIN_STEP_DB;
    if (db == s_gain_db) return;
    if (audio_in_set_gain_db((float)db) == ESP_OK) {
        s_gain_db = db;
        cfg_set("gain", db);
        config_changed();
    }
}

int app_screen_timeout_s(void)
{
    return s_screen_timeout_s;
}

void app_set_screen_timeout_s(int seconds)
{
    if (seconds < 0) seconds = 0;
    s_screen_timeout_s = seconds;
    cfg_set("scr_off", seconds);
    config_changed();
}

void app_language(char out[APP_LANG_MAX + 1])
{
    strlcpy(out, s_lang, APP_LANG_MAX + 1);
}

void app_set_language(const char *code)
{
    if (!settings_lang_valid(code)) return;
    strlcpy(s_lang, code, sizeof(s_lang));
    cfg_set_str("lang", s_lang);
    config_changed();
}

int app_rec_format(void)
{
    return s_rec_format;
}

void app_set_rec_format(int fmt)
{
    if (fmt < REC_FMT_WAV || fmt > REC_FMT_MP3) return;
    s_rec_format = fmt; // takes effect from the next recording
    cfg_set("fmt", fmt);
    config_changed();
}

uint32_t app_rec_bytes_per_s(void)
{
    return s_rec_format == REC_FMT_OPUS ? REC_OPUS_BITRATE / 8 : s_rec_format == REC_FMT_MP3 ? REC_MP3_KBPS * 1000 / 8 : SAMPLE_RATE_HZ * 2;
}

bool app_filter_enabled(void)
{
    return s_filter;
}

void app_set_filter_enabled(bool on)
{
    s_filter = on;
    cfg_set("hpf", on ? 1 : 0);
    config_changed();
}

bool app_autodelete_enabled(void)
{
    return s_autodelete;
}

void app_set_autodelete_enabled(bool on)
{
    s_autodelete = on;
    cfg_set("autodel", on ? 1 : 0);
    config_changed();
}

bool app_lock_enabled(void)
{
    return s_lock_on;
}

bool app_set_lock_enabled(bool on)
{
    if (on && !settings_lock_pin_valid(s_lock_pin)) return false;
    s_lock_on = on;
    cfg_set("lock_on", on ? 1 : 0);
    config_changed();
    return true;
}

void app_lock_pin(char out[APP_LOCK_PIN_MAX + 1])
{
    strlcpy(out, s_lock_pin, APP_LOCK_PIN_MAX + 1);
}

bool app_set_lock_pin(const char *pin)
{
    if (!settings_lock_pin_valid(pin)) return false;
    strlcpy(s_lock_pin, pin, sizeof(s_lock_pin));
    cfg_set_str("lock_pin", s_lock_pin);
    config_changed();
    return true;
}

int app_lock_after_s(void)
{
    return s_lock_after_s;
}

void app_set_lock_after_s(int seconds)
{
    if (seconds != 60 && seconds != 300 && seconds != 900) return;
    s_lock_after_s = seconds;
    cfg_set("lock_after", seconds);
    config_changed();
}

int app_theme(void)
{
    return s_theme;
}

void app_set_theme(int theme)
{
    s_theme = theme ? 1 : 0;
    cfg_set("theme", s_theme);
    config_changed();
}

int app_brightness(void)
{
    return s_brightness;
}

void app_set_brightness(int percent, bool save)
{
    if (percent < DISPLAY_BRIGHTNESS_MIN) percent = DISPLAY_BRIGHTNESS_MIN;
    if (percent > 100) percent = 100;
    s_brightness = percent;
    display_set_brightness(percent);
    if (save) {
        cfg_set("bright", percent);
        config_changed();
    }
}

// ---------- Recordings on the card ----------

// Recognizes "REC_dddd.<ext>" and "REC_dddd <name>.<ext>" (WAV, OPUS, MP3). Returns the number and name.
static bool parse_recording_file(const char *file, int *id, char *title, size_t title_len)
{
    size_t len = strlen(file);
    int fmt = rec_format_from_name(file);
    if (fmt < 0) return false;
    size_t ext = strlen(rec_format_ext((rec_format_t)fmt));
    if (len < 8 + ext || strncasecmp(file, "REC_", 4) != 0) return false;
    int n = 0;
    for (int i = 4; i < 8; i++) {
        if (file[i] < '0' || file[i] > '9') return false;
        n = n * 10 + (file[i] - '0');
    }
    if (len == 8 + ext) {
        if (title && title_len) title[0] = 0;
    } else {
        if (file[8] != ' ' || len < 9 + ext) return false;
        size_t tl = len - ext - 9; // between "REC_dddd " and the extension
        if (tl == 0) return false;
        if (title && title_len) {
            if (tl >= title_len) tl = title_len - 1;
            memcpy(title, file + 9, tl);
            title[tl] = 0;
        }
    }
    if (id) *id = n;
    return true;
}

static int cmp_recording_desc(const void *a, const void *b)
{
    return ((const app_recording_t *)b)->id - ((const app_recording_t *)a)->id;
}

int app_list_recordings(app_recording_t *out, int max)
{
    if (!storage_is_mounted()) return 0;
    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (!dir) {
        ESP_LOGW(TAG, "list: opendir failed (errno %d)", errno);
        return 0;
    }

    // Keeps the `max` most recent (highest number), whatever the order on the card.
    int n = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        app_recording_t r = {0};
        if (strlen(e->d_name) >= sizeof(r.file) || !parse_recording_file(e->d_name, &r.id, r.title, sizeof(r.title))) continue;
        int slot = n;
        if (n == max) {
            slot = 0;
            for (int i = 1; i < n; i++) {
                if (out[i].id < out[slot].id) slot = i;
            }
            if (r.id <= out[slot].id) continue; // older than all those already kept
        }
        char path[128];
        struct stat st;
        snprintf(path, sizeof(path), STORAGE_MOUNT_POINT "/%s", e->d_name);
        if (stat(path, &st) != 0 || st.st_size < MIN_LISTED_BYTES) continue;
        strlcpy(r.file, e->d_name, sizeof(r.file));
        r.bytes = (uint32_t)st.st_size;
        r.seconds = rec_seconds_from_size((rec_format_t)rec_format_from_name(e->d_name), SAMPLE_RATE_HZ, (uint64_t)st.st_size);
        // Without a set clock FAT writes 1980; treat it as "no date".
        r.mtime = st.st_mtime > 1700000000 ? st.st_mtime : 0;
        out[slot] = r;
        if (slot == n) n++;
    }
    closedir(dir);
    qsort(out, (size_t)n, sizeof(out[0]), cmp_recording_desc);
    return n;
}

// Finds a recording's file by number, whether it has a name or not.
static bool find_recording_file(int id, char *file, size_t len)
{
    if (!storage_is_mounted()) return false;
    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (!dir) return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(dir)) != NULL) {
        int n;
        if (parse_recording_file(e->d_name, &n, NULL, 0) && n == id && strlen(e->d_name) < len) {
            strlcpy(file, e->d_name, len);
            found = true;
        }
    }
    closedir(dir);
    return found;
}

bool app_recording_path(int id, char *out, size_t len)
{
    char file[96];
    if (!find_recording_file(id, file, sizeof(file))) return false;
    return snprintf(out, len, STORAGE_MOUNT_POINT "/%s", file) < (int)len;
}

// Next free number: one more than the highest existing, with or without a name.
static bool next_recording_path(char *out, size_t len)
{
    if (!storage_is_mounted()) return false;
    DIR *dir = opendir(STORAGE_MOUNT_POINT);
    if (!dir) return false;
    int max_id = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        int n;
        if (parse_recording_file(e->d_name, &n, NULL, 0) && n > max_id) max_id = n;
    }
    closedir(dir);
    if (max_id >= 9999) return false;
    return snprintf(out, len, STORAGE_MOUNT_POINT "/REC_%04d%s", max_id + 1, rec_format_ext((rec_format_t)s_rec_format)) < (int)len;
}

bool app_delete_recording(int id)
{
    char path[128];
    if (s_recording || !app_recording_path(id, path, sizeof(path))) return false;
    bool ok = unlink(path) == 0;
    refresh_space();
    return ok;
}

// Cleans the user-given name: removes what FAT rejects in file names, leading/trailing
// spaces and a trailing dot, and cuts at APP_TITLE_MAX without splitting a UTF-8 character.
static void sanitize_title(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    while (*in == ' ') in++;
    for (; *in && o + 1 < out_len; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20 || strchr("\\/:*?\"<>|", c)) continue;
        if (c == ' ' && o > 0 && out[o - 1] == ' ') continue; // no double spaces
        out[o++] = (char)c;
    }
    if (*in) {
        // Truncated by length: the last character may be cut in half; drop it entirely.
        while (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0x80) o--;
        if (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0xC0) o--;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.')) o--;
    out[o] = 0;
}

bool app_rename_recording(int id, const char *title)
{
    char old_file[96], old_path[128], new_path[128], clean[APP_TITLE_MAX + 1];
    if (s_recording || !title || !find_recording_file(id, old_file, sizeof(old_file))) return false;
    sanitize_title(title, clean, sizeof(clean));
    snprintf(old_path, sizeof(old_path), STORAGE_MOUNT_POINT "/%s", old_file);
    const char *ext = rec_format_ext((rec_format_t)rec_format_from_name(old_file)); // the format doesn't change
    if (clean[0]) {
        snprintf(new_path, sizeof(new_path), STORAGE_MOUNT_POINT "/REC_%04d %s%s", id, clean, ext);
    } else {
        snprintf(new_path, sizeof(new_path), STORAGE_MOUNT_POINT "/REC_%04d%s", id, ext);
    }
    if (strcmp(old_path, new_path) == 0) return true;
    return rename(old_path, new_path) == 0;
}

bool app_format_card(void)
{
    if (s_recording) return false;
    bool ok = storage_format() == ESP_OK;
    refresh_space();
    if (ok) config_changed(); // the config file went along: recreate it
    return ok;
}

void app_web_user(char out[APP_WEB_USER_MAX + 1])
{
    strlcpy(out, s_web_user, APP_WEB_USER_MAX + 1);
}

void app_web_pass(char out[APP_WEB_PASS_MAX + 1])
{
    strlcpy(out, s_web_pass, APP_WEB_PASS_MAX + 1);
}

static void cfg_set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

static bool cfg_get_str(const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, key, out, &len) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool app_set_web_user(const char *user)
{
    if (!settings_user_valid(user)) return false;
    strlcpy(s_web_user, user, sizeof(s_web_user));
    cfg_set_str("web_user", s_web_user);
    web_set_credentials(s_web_user, s_web_pass);
    config_changed();
    return true;
}

bool app_set_web_pass(const char *pass)
{
    if (!settings_pass_valid(pass)) return false;
    strlcpy(s_web_pass, pass, sizeof(s_web_pass));
    cfg_set_str("web_pass", s_web_pass);
    web_set_credentials(s_web_user, s_web_pass);
    config_changed();
    return true;
}

void app_ntp_server(char out[APP_NTP_MAX + 1])
{
    strlcpy(out, s_ntp, APP_NTP_MAX + 1);
}

bool app_set_ntp_server(const char *server)
{
    if (!settings_ntp_valid(server)) return false;
    strlcpy(s_ntp, server, sizeof(s_ntp));
    cfg_set_str("ntp", s_ntp);
    net_set_ntp_server(s_ntp);
    config_changed();
    return true;
}

bool app_dev_enabled(void)
{
    return s_dev_on;
}

void app_set_dev_enabled(bool on)
{
    s_dev_on = on;
    cfg_set("dev_on", on ? 1 : 0);
    config_changed();
}

bool app_ap_enabled(void)
{
    return s_ap_on;
}

void app_set_ap_enabled(bool on)
{
    s_ap_on = on;
    cfg_set("ap_on", on ? 1 : 0);
    net_ap_set(s_ap_on, s_ap_ssid, s_ap_pass);
    config_changed();
}

void app_ap_ssid(char out[APP_AP_SSID_MAX + 1])
{
    strlcpy(out, s_ap_ssid, APP_AP_SSID_MAX + 1);
}

void app_ap_pass(char out[APP_AP_PASS_MAX + 1])
{
    strlcpy(out, s_ap_pass, APP_AP_PASS_MAX + 1);
}

bool app_set_ap_ssid(const char *ssid)
{
    if (!settings_ap_ssid_valid(ssid)) return false;
    strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    cfg_set_str("ap_ssid", s_ap_ssid);
    net_ap_set(s_ap_on, s_ap_ssid, s_ap_pass);
    config_changed();
    return true;
}

bool app_set_ap_pass(const char *pass)
{
    if (!settings_ap_pass_valid(pass)) return false;
    strlcpy(s_ap_pass, pass, sizeof(s_ap_pass));
    cfg_set_str("ap_pass", s_ap_pass);
    net_ap_set(s_ap_on, s_ap_ssid, s_ap_pass);
    config_changed();
    return true;
}

void app_hostname(char out[APP_HOSTNAME_MAX + 1])
{
    strlcpy(out, s_hostname, APP_HOSTNAME_MAX + 1);
}

bool app_set_hostname(const char *name)
{
    if (!settings_host_valid(name)) return false;
    strlcpy(s_hostname, name, sizeof(s_hostname));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "host", s_hostname);
        nvs_commit(h);
        nvs_close(h);
    }
    web_set_hostname(s_hostname);
    config_changed();
    return true;
}

bool app_web_enabled(void)
{
    return s_web_enabled;
}

void app_set_web_enabled(bool enabled)
{
    s_web_enabled = enabled;
    cfg_set("web_on", enabled ? 1 : 0);
    config_changed();
    if (!enabled) {
        web_stop();
    } else if (s_net_ready && web_start(s_web_user, s_web_pass, s_hostname) != ESP_OK) {
        ESP_LOGW(TAG, "web: did not start");
    }
}

bool app_retry_card(void)
{
    bool ok = storage_mount() == ESP_OK;
    refresh_space();
    return ok;
}

// ---------- Audio and recording ----------

static void button_task(void *arg)
{
    bool pressed = false;
    int stable_ms = 0;
    for (;;) {
        bool now = gpio_get_level(PIN_BOOT) == 0;
        if (now != pressed) {
            stable_ms += 10;
            if (stable_ms >= DEBOUNCE_MS) {
                pressed = now;
                stable_ms = 0;
                if (pressed) s_toggle_requested = true;
            }
        } else {
            stable_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static float to_dbfs(double amplitude)
{
    if (amplitude < 1.0) return -96.0f;
    return (float)(20.0 * log10(amplitude / 32768.0));
}

static void audio_task(void *arg)
{
    static int16_t block[BLOCK_SAMPLES];
    static dsp_highpass_t hpf;
    dsp_highpass_init(&hpf, SAMPLE_RATE_HZ, 100);

    for (;;) {
        if (audio_in_read(block, BLOCK_SAMPLES) != BLOCK_SAMPLES) {
            ESP_LOGE(TAG, "audio read failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (s_filter) dsp_highpass_run(&hpf, block, BLOCK_SAMPLES); // always runs, to avoid a click when turned on

        if (s_recording) {
            // Whole block or nothing, so the file never ends up with half a sample.
            if (xStreamBufferSpacesAvailable(s_stream) >= BLOCK_BYTES) {
                xStreamBufferSend(s_stream, block, BLOCK_BYTES, 0);
            } else {
                s_dropped_blocks++;
            }
        }

        int peak = 0;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int v = block[i] < 0 ? -block[i] : block[i];
            if (v > peak) peak = v;
        }
        s_level_dbfs = to_dbfs(peak);
    }
}

// Card full with "delete oldest" on: deletes starting from the lowest-numbered recording
// (never `keep`) until APP_AUTODELETE_MIN_MB is free. Returns how many were deleted.
static int free_space_by_deleting(const char *keep)
{
    if (!s_autodelete) return 0;
    int deleted = 0;
    for (;;) {
        refresh_space();
        if (s_free_mb >= APP_AUTODELETE_MIN_MB) break;
        int oldest = -1;
        char oldest_file[128] = "";
        DIR *dir = opendir(STORAGE_MOUNT_POINT);
        if (!dir) break;
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            int id;
            if (!parse_recording_file(e->d_name, &id, NULL, 0)) continue;
            if (keep && strstr(keep, e->d_name)) continue;
            if (oldest < 0 || id < oldest) {
                oldest = id;
                strlcpy(oldest_file, e->d_name, sizeof(oldest_file));
            }
        }
        closedir(dir);
        if (oldest < 0) break;
        char path[160];
        snprintf(path, sizeof(path), STORAGE_MOUNT_POINT "/%s", oldest_file);
        if (unlink(path) != 0) break;
        ESP_LOGI(TAG, "card full: deleted %s", oldest_file);
        deleted++;
    }
    return deleted;
}

static void recorder_task(void *arg)
{
    static uint8_t chunk[BLOCK_BYTES];
    rec_writer_t *wav = NULL;
    int blocks_since_space_check = 0;
    char path[40];
    int64_t rec_samples = 0;
    int blocks_since_checkpoint = 0;
    bool was_mounted = true;

    for (;;) {
        bool stop = false;
        app_event_t stop_event = APP_EVENT_SAVED;

        if (s_toggle_requested) {
            s_toggle_requested = false;
            if (wav) {
                stop = true;
            } else if (storage_in_use_by_host()) {
                raise_event(APP_EVENT_CARD_BUSY, 0);
            } else if (free_space_by_deleting(NULL), !next_recording_path(path, sizeof(path))) {
                raise_event(APP_EVENT_NO_CARD, 0);
            } else if ((wav = rec_writer_open(path, (rec_format_t)s_rec_format, SAMPLE_RATE_HZ)) == NULL) {
                raise_event(errno == ENOSPC ? APP_EVENT_CARD_FULL : APP_EVENT_WRITE_ERROR, 0);
            } else {
                const char *name = path + sizeof(STORAGE_MOUNT_POINT); // after "/sdcard/"
                strlcpy(s_rec_name, name, 9);                         // "REC_0023"
                printf("recording: %s\n", path);
                rec_samples = 0;
                blocks_since_checkpoint = 0;
                s_rec_seconds = 0;
                s_rec_bytes = 0;
                s_dropped_blocks = 0;
                xStreamBufferReset(s_stream);
                storage_usb_enable(false); // the computer must not take the card mid-file
                s_recording = true;
            }
        }

        if (wav) {
            if (stop) s_recording = false; // stop feeding; the loop below drains the rest

            size_t got;
            TickType_t wait = stop ? 0 : pdMS_TO_TICKS(100);
            while ((got = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), wait)) > 0) {
                bool failed = rec_writer_write(wav, (const int16_t *)chunk, got / sizeof(int16_t)) != 0;
                if (!failed) {
                    rec_samples += got / sizeof(int16_t);
                    s_rec_seconds = (uint32_t)(rec_samples / SAMPLE_RATE_HZ);
                    s_rec_bytes = (uint32_t)rec_writer_bytes(wav);
                    if (++blocks_since_checkpoint >= CHECKPOINT_BLOCKS) {
                        blocks_since_checkpoint = 0;
                        failed = rec_writer_checkpoint(wav) != 0;
                    }
                    if (s_autodelete && ++blocks_since_space_check >= 300) { // every 30 s
                        blocks_since_space_check = 0;
                        free_space_by_deleting(path);
                    }
                }
                if (failed) {
                    stop = true;
                    stop_event = errno == ENOSPC ? APP_EVENT_CARD_FULL : APP_EVENT_WRITE_ERROR;
                    s_recording = false;
                    break;
                }
                wait = 0; // data already available: drain without waiting
                if (!stop && s_toggle_requested) break;
            }

            if (stop) {
                int rc = rec_writer_close(wav);
                wav = NULL;
                storage_usb_enable(true);
                refresh_space();
                printf("saved: %s %lus dropped=%lu%s\n", path, (unsigned long)s_rec_seconds,
                       (unsigned long)s_dropped_blocks, rc == 0 ? "" : " (close failed)");
                raise_event(stop_event, s_rec_seconds);
            }
        } else {
            // The card came back from the computer: refresh the free space.
            bool mounted = storage_is_mounted();
            if (mounted && !was_mounted) refresh_space();
            was_mounted = mounted;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// Periodic free-memory log, to diagnose internal RAM shortage.
static void mem_task(void *arg)
{
    for (;;) {
        ESP_LOGI(TAG, "mem: internal free=%u KB (min %u KB), psram free=%u KB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void app_main(void)
{
    printf("recorder %s\n", APP_VERSION);

    logbuf_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) printf("nvs: failed (%s)\n", esp_err_to_name(err));
    s_gain_db = cfg_get("gain", DEFAULT_GAIN_DB);
    s_screen_timeout_s = cfg_get("scr_off", DEFAULT_SCREEN_TIMEOUT_S);
    s_theme = cfg_get("theme", 0) ? 1 : 0;
    int fmt = cfg_get("fmt", REC_FMT_MP3);
    s_rec_format = fmt >= REC_FMT_WAV && fmt <= REC_FMT_MP3 ? fmt : REC_FMT_MP3;
    s_filter = cfg_get("hpf", 1) != 0;
    s_autodelete = cfg_get("autodel", 0) != 0;
    {
        char pin[APP_LOCK_PIN_MAX + 1];
        if (cfg_get_str("lock_pin", pin, sizeof(pin)) && settings_lock_pin_valid(pin)) strlcpy(s_lock_pin, pin, sizeof(s_lock_pin));
        s_lock_on = cfg_get("lock_on", 0) != 0 && s_lock_pin[0];
        int after = cfg_get("lock_after", 300);
        s_lock_after_s = after == 60 || after == 900 ? after : 300;
    }
    app_set_brightness(cfg_get("bright", 100), false); // takes effect once the screen turns on
    char saved_cred[APP_WEB_PASS_MAX + 1];
    if (cfg_get_str("web_user", saved_cred, sizeof(saved_cred)) && settings_user_valid(saved_cred)) {
        strlcpy(s_web_user, saved_cred, sizeof(s_web_user));
    }
    if (cfg_get_str("web_pass", saved_cred, sizeof(saved_cred)) && settings_pass_valid(saved_cred)) {
        strlcpy(s_web_pass, saved_cred, sizeof(s_web_pass));
    } else {
        // First time (or an old version that stored only the numeric password): keep the same PIN.
        int pin = cfg_get("web_pin", -1);
        if (pin < 0) pin = (int)(esp_random() % 1000000);
        snprintf(s_web_pass, sizeof(s_web_pass), "%06d", pin);
        cfg_set_str("web_pass", s_web_pass);
    }
    s_web_enabled = cfg_get("web_on", 1) != 0;
    char saved_str[APP_AP_PASS_MAX + 1];
    if (cfg_get_str("ntp", saved_str, sizeof(saved_str)) && settings_ntp_valid(saved_str)) strlcpy(s_ntp, saved_str, sizeof(s_ntp));
    if (cfg_get_str("lang", saved_str, sizeof(saved_str)) && settings_lang_valid(saved_str)) strlcpy(s_lang, saved_str, sizeof(s_lang));
    s_ap_on = cfg_get("ap_on", 0) != 0;
    s_dev_on = cfg_get("dev_on", 1) != 0;
    if (cfg_get_str("ap_ssid", saved_str, sizeof(saved_str)) && settings_ap_ssid_valid(saved_str)) {
        strlcpy(s_ap_ssid, saved_str, sizeof(s_ap_ssid));
    } else {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac); // a different name for each device
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "P4Recorder-%02X%02X", mac[4], mac[5]);
        cfg_set_str("ap_ssid", s_ap_ssid);
    }
    if (cfg_get_str("ap_pass", saved_str, sizeof(saved_str)) && settings_ap_pass_valid(saved_str)) {
        strlcpy(s_ap_pass, saved_str, sizeof(s_ap_pass));
    } else {
        snprintf(s_ap_pass, sizeof(s_ap_pass), "%08lu", (unsigned long)(esp_random() % 100000000));
        cfg_set_str("ap_pass", s_ap_pass);
    }
    net_set_ntp_server(s_ntp);
    net_ap_set(s_ap_on, s_ap_ssid, s_ap_pass);
    nvs_handle_t host_nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &host_nvs) == ESP_OK) {
        char saved[APP_HOSTNAME_MAX + 1];
        size_t len = sizeof(saved);
        if (nvs_get_str(host_nvs, "host", saved, &len) == ESP_OK && settings_host_valid(saved)) strlcpy(s_hostname, saved, sizeof(s_hostname));
        nvs_close(host_nvs);
    }

    gpio_config_t in = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);

    esp_err_t disp_err = display_init();
    printf("display: %s\n", disp_err == ESP_OK ? "ready" : esp_err_to_name(disp_err));

    if (storage_mount() == ESP_OK) {
        refresh_space();
        printf("sd: mounted, %lu MB free\n", (unsigned long)s_free_mb);
    } else {
        printf("sd: failed\n");
    }

    err = audio_in_init(SAMPLE_RATE_HZ);
    if (err != ESP_OK) {
        printf("audio: failed (%s)\n", esp_err_to_name(err));
    } else {
        audio_in_set_gain_db((float)s_gain_db);
        printf("audio: ready, gain %d dB\n", s_gain_db);
    }

    static StaticStreamBuffer_t stream_struct;
    uint8_t *buffer = heap_caps_malloc(BUFFER_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (!buffer) buffer = heap_caps_malloc(BUFFER_BYTES + 1, MALLOC_CAP_DEFAULT);
    if (!buffer) {
        printf("memory: no room for the audio buffer\n");
        return;
    }
    s_stream = xStreamBufferCreateStatic(BUFFER_BYTES + 1, 1, buffer, &stream_struct);

    xTaskCreate(button_task, "botao", 2048, NULL, 5, NULL);
    if (err == ESP_OK) xTaskCreate(audio_task, "audio", 8192, NULL, 7, NULL);
    // Large stack and pinned core: Opus uses the P4's vector instructions (PIE), whose state
    // is only saved on task switches when the task doesn't change cores.
    xTaskCreatePinnedToCore(recorder_task, "recorder", 32 * 1024, NULL, 5, NULL, 1);

    i18n_init(s_lang); // after the card: extra languages come from the LANGUAGES folder
    if (disp_err == ESP_OK) ui_init();

    xTaskCreate(mem_task, "mem", 3072, NULL, 1, NULL);

    config_init(); // reads CONFIG.JSON from the card before the network and server use the settings
    esp_err_t net_err = net_init();
    printf("network: %s\n", net_err == ESP_OK ? "radio on" : esp_err_to_name(net_err));
    s_net_ready = net_err == ESP_OK;
    if (s_net_ready && s_web_enabled) {
        esp_err_t web_err = web_start(s_web_user, s_web_pass, s_hostname);
        // Username and password are shown only on the device screen.
        printf("web: %s at http://%s.local\n", web_err == ESP_OK ? "server up" : esp_err_to_name(web_err), s_hostname);
    } else {
        printf("web: off\n");
    }

    // Made it this far: firmware is good. Without this confirmation, the next reboot rolls back.
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) printf("firmware: confirmed\n");
}
