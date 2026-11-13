#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app.h"
#include "display.h"
#include "net.h"
#include "settings_file.h"
#include "storage.h"

#define CONFIG_PATH STORAGE_MOUNT_POINT "/CONFIG.JSON"
#define CONFIG_TMP STORAGE_MOUNT_POINT "/CONFIG.TMP"
#define CONFIG_MAX_BYTES 8192
#define WRITE_DELAY_MS 1500 // batches consecutive changes (e.g. gain tapped several times)

_Static_assert(SETTINGS_HOST_MAX == APP_HOSTNAME_MAX, "hostname rules");
_Static_assert(SETTINGS_USER_MAX == APP_WEB_USER_MAX, "username rules");
_Static_assert(SETTINGS_PASS_MIN == APP_WEB_PASS_MIN && SETTINGS_PASS_MAX == APP_WEB_PASS_MAX, "password rules");
_Static_assert(SETTINGS_WIFI_MAX == NET_MAX_SAVED, "saved networks");
_Static_assert(SETTINGS_LANG_MAX == APP_LANG_MAX, "language");
_Static_assert(SETTINGS_LOCK_PIN_MAX == APP_LOCK_PIN_MAX, "lock code");
_Static_assert(SETTINGS_NTP_MAX == APP_NTP_MAX && SETTINGS_SSID_MAX == APP_AP_SSID_MAX && SETTINGS_AP_PASS_MAX == APP_AP_PASS_MAX, "clock and own network");
_Static_assert(SETTINGS_BRIGHTNESS_MIN == DISPLAY_BRIGHTNESS_MIN, "brightness");
_Static_assert(SETTINGS_GAIN_MIN_DB == APP_GAIN_MIN_DB && SETTINGS_GAIN_MAX_DB == APP_GAIN_MAX_DB && SETTINGS_GAIN_STEP_DB == APP_GAIN_STEP_DB, "gain");

static const char *TAG = "config";

static SemaphoreHandle_t s_lock; // file and notice
static volatile bool s_dirty;
static volatile TickType_t s_changed_at;
static bool s_applying;         // changes coming from the file itself
static config_notice_t s_notice; // CONFIG_NOTICE_NONE: nothing pending
static int s_notice_ignored;

void config_changed(void)
{
    if (s_applying) return;
    s_changed_at = xTaskGetTickCount();
    s_dirty = true;
}

static void saved_changed(void)
{
    config_changed();
}

// The notice text is built by the screen (in its language); only the kind is kept here.
bool config_take_notice(config_notice_t *kind, int *ignored)
{
    if (!s_lock || s_notice == CONFIG_NOTICE_NONE) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool any = s_notice != CONFIG_NOTICE_NONE;
    if (any) {
        *kind = s_notice;
        *ignored = s_notice_ignored;
        s_notice = CONFIG_NOTICE_NONE;
    }
    xSemaphoreGive(s_lock);
    return any;
}

static void set_notice(config_notice_t kind, int ignored)
{
    s_notice = kind;
    s_notice_ignored = ignored;
}

// ---------- Current state <-> settings_t ----------

static void current(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    app_status_t st;
    app_get_status(&st);
    s->brightness = app_brightness();
    s->screen_off_s = app_screen_timeout_s();
    s->gain_db = st.gain_db;
    s->theme = app_theme();
    s->rec_format = app_rec_format();
    s->filter = app_filter_enabled();
    s->autodelete = app_autodelete_enabled();
    s->lock_on = app_lock_enabled();
    app_lock_pin(s->lock_pin);
    s->lock_after_s = app_lock_after_s();
    app_language(s->lang);
    s->web_on = app_web_enabled();
    s->dev_on = app_dev_enabled();
    app_hostname(s->host);
    app_web_user(s->user);
    app_web_pass(s->pass);
    app_ntp_server(s->ntp);
    s->ap_on = app_ap_enabled();
    app_ap_ssid(s->ap_ssid);
    app_ap_pass(s->ap_pass);
    net_saved_t saved[NET_MAX_SAVED];
    s->wifi_count = net_saved_list(saved, NET_MAX_SAVED);
    for (int i = 0; i < s->wifi_count; i++) {
        strlcpy(s->wifi[i].ssid, saved[i].ssid, sizeof(s->wifi[i].ssid));
        strlcpy(s->wifi[i].pass, saved[i].password, sizeof(s->wifi[i].pass));
    }
}

static void apply(const settings_t *cur, const settings_t *s)
{
    s_applying = true;
    if (s->brightness != cur->brightness) app_set_brightness(s->brightness, true);
    if (s->screen_off_s != cur->screen_off_s) app_set_screen_timeout_s(s->screen_off_s);
    if (s->gain_db != cur->gain_db) app_set_gain_db(s->gain_db);
    if (s->rec_format != cur->rec_format) app_set_rec_format(s->rec_format);
    if (s->filter != cur->filter) app_set_filter_enabled(s->filter);
    if (s->autodelete != cur->autodelete) app_set_autodelete_enabled(s->autodelete);
    if (strcmp(s->lock_pin, cur->lock_pin)) app_set_lock_pin(s->lock_pin);
    if (s->lock_after_s != cur->lock_after_s) app_set_lock_after_s(s->lock_after_s);
    if (s->lock_on != cur->lock_on) app_set_lock_enabled(s->lock_on);
    if (s->theme != cur->theme) app_set_theme(s->theme); // the screen applies it on the next cycle
    if (strcmp(s->lang, cur->lang)) app_set_language(s->lang);
    if (strcmp(s->host, cur->host)) app_set_hostname(s->host);
    if (strcmp(s->user, cur->user)) app_set_web_user(s->user);
    if (strcmp(s->pass, cur->pass)) app_set_web_pass(s->pass);
    if (s->web_on != cur->web_on) app_set_web_enabled(s->web_on);
    if (s->dev_on != cur->dev_on) app_set_dev_enabled(s->dev_on);
    if (strcmp(s->ntp, cur->ntp)) app_set_ntp_server(s->ntp);
    if (strcmp(s->ap_ssid, cur->ap_ssid)) app_set_ap_ssid(s->ap_ssid);
    if (strcmp(s->ap_pass, cur->ap_pass)) app_set_ap_pass(s->ap_pass);
    if (s->ap_on != cur->ap_on) app_set_ap_enabled(s->ap_on);
    bool wifi_same = s->wifi_count == cur->wifi_count;
    for (int i = 0; wifi_same && i < s->wifi_count; i++) {
        wifi_same = !strcmp(s->wifi[i].ssid, cur->wifi[i].ssid) && !strcmp(s->wifi[i].pass, cur->wifi[i].pass);
    }
    if (!wifi_same) {
        net_saved_t list[NET_MAX_SAVED] = {0};
        for (int i = 0; i < s->wifi_count; i++) {
            strlcpy(list[i].ssid, s->wifi[i].ssid, sizeof(list[i].ssid));
            strlcpy(list[i].password, s->wifi[i].pass, sizeof(list[i].password));
        }
        net_saved_set(list, s->wifi_count);
    }
    s_applying = false;
}

// ---------- File ----------

// File contents (malloc) or NULL. *too_big if over the limit; *missing only if the
// file does not exist (other errors must not lead to recreating it and erasing what the user wrote).
static char *read_file(const char *path, bool *too_big, bool *missing)
{
    *too_big = false;
    *missing = false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        *missing = errno == ENOENT;
        return NULL;
    }
    char *buf = malloc(CONFIG_MAX_BYTES + 1);
    size_t n = buf ? fread(buf, 1, CONFIG_MAX_BYTES + 1, f) : 0;
    fclose(f);
    if (!buf) return NULL;
    if (n > CONFIG_MAX_BYTES) {
        *too_big = true;
        free(buf);
        return NULL;
    }
    buf[n] = 0;
    return buf;
}

// Writes the current state if the file differs. false on write error.
static bool write_current(void)
{
    settings_t s;
    current(&s);
    char *text = settings_to_json(&s);
    if (!text) return false;
    bool too_big, missing;
    char *old = read_file(CONFIG_PATH, &too_big, &missing);
    bool same = old && strcmp(old, text) == 0;
    free(old);
    bool ok = true;
    if (!same) {
        // New file alongside, swapped at the end: a power loss midway won't leave half a file.
        FILE *f = fopen(CONFIG_TMP, "wb");
        size_t len = strlen(text);
        ok = f && fwrite(text, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
        if (f) fclose(f);
        if (ok) {
            unlink(CONFIG_PATH);
            ok = rename(CONFIG_TMP, CONFIG_PATH) == 0;
        } else {
            unlink(CONFIG_TMP);
        }
        ESP_LOGI(TAG, "%s %s", ok ? "written" : "failed to write", CONFIG_PATH);
    }
    free(text);
    // Hidden on the computer so it isn't deleted by mistake (also the one from older versions).
    // The card is FATFS drive 0.
    if (ok && f_chmod("0:/CONFIG.JSON", AM_HID, AM_HID) != FR_OK) ESP_LOGW(TAG, "could not hide the file");
    return ok;
}

// Reads the file and applies what changed; then makes the file match the device state.
static void import_file(void)
{
    ESP_LOGI(TAG, "reading %s", CONFIG_PATH);
    bool too_big, missing;
    char *text = read_file(CONFIG_PATH, &too_big, &missing);
    if (!text && missing) {
        write_current(); // does not exist yet (new or formatted card)
        return;
    }
    if (!text && !too_big) {
        ESP_LOGW(TAG, "could not read %s (errno %d)", CONFIG_PATH, errno);
        return;
    }
    settings_t cur, next;
    current(&cur);
    int ignored = 0;
    if (too_big || !settings_from_json(text, &cur, &next, &ignored)) {
        free(text);
        ESP_LOGW(TAG, "%s has errors; keeping the device settings", CONFIG_PATH);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_notice(CONFIG_NOTICE_UNREADABLE, 0);
        xSemaphoreGive(s_lock);
        return; // don't overwrite: the user may want to fix what they typed
    }
    free(text);

    bool changed = !settings_equal(&cur, &next);
    if (changed) apply(&cur, &next);
    if (changed || ignored) {
        config_notice_t kind = changed && ignored ? CONFIG_NOTICE_APPLIED_IGNORED
                               : changed          ? CONFIG_NOTICE_APPLIED
                                                  : CONFIG_NOTICE_IGNORED;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_notice(kind, ignored);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "file applied (changed=%d, ignored=%d)", changed, ignored);
    }
    write_current(); // file tidied up: invalid values revert to the device's current values
}

static void config_task(void *arg)
{
    bool was_mounted = (bool)(intptr_t)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        bool mounted = storage_is_mounted();
        if (mounted != was_mounted) ESP_LOGI(TAG, "card %s", mounted ? "returned to the device" : "left the device");
        if (mounted && !was_mounted) import_file(); // card returned (USB drive ejected, new card)
        was_mounted = mounted;
        if (s_dirty && mounted && (xTaskGetTickCount() - s_changed_at) >= pdMS_TO_TICKS(WRITE_DELAY_MS)) {
            s_dirty = false;
            if (!write_current()) {
                s_changed_at = xTaskGetTickCount(); // retry after the same delay
                s_dirty = true;
            }
        }
    }
}

void config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    net_on_saved_changed(saved_changed);
    bool mounted = storage_is_mounted();
    if (mounted) import_file();
    if (xTaskCreate(config_task, "config", 6144, (void *)(intptr_t)mounted, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task not created");
    } else {
        ESP_LOGI(TAG, "watching the card (held by device: %d)", mounted);
    }
}
