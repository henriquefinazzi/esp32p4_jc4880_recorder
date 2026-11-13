#include "updater.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fwversion.h"
#include "net.h"

#define API_URL "https://api.github.com/repos/" UPDATER_REPO "/releases/latest"
#define MAX_JSON (96 * 1024)
#define USER_AGENT "P4-Recorder/" APP_VERSION

static const char *TAG = "updater";

static volatile upd_state_t s_state = UPD_IDLE;
static volatile int s_progress;
static char s_latest[24];
static char s_asset_url[512];
static int s_asset_size;
static const char *s_error = "update.err.network";

upd_state_t updater_state(void) { return s_state; }
const char *updater_latest(void) { return s_latest; }
int updater_progress(void) { return s_progress; }
const char *updater_error_key(void) { return s_error; }

static void fail(const char *key)
{
    s_error = key;
    s_state = UPD_ERROR;
}

// Reads the whole response body into a PSRAM buffer. NULL on error.
static char *http_get(const char *url, int *status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    esp_http_client_set_header(c, "Accept", "application/vnd.github+json");
    char *buf = NULL;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        buf = heap_caps_malloc(MAX_JSON + 1, MALLOC_CAP_SPIRAM);
        int len = 0, n;
        while (buf && len < MAX_JSON && (n = esp_http_client_read(c, buf + len, MAX_JSON - len)) > 0) len += n;
        if (buf) buf[len] = 0;
    }
    esp_http_client_cleanup(c);
    return buf;
}

static void check_task(void *arg)
{
    int status = 0;
    char *body = http_get(API_URL, &status);
    if (!body) {
        fail("update.err.network");
        vTaskDelete(NULL);
        return;
    }
    if (status == 404) {
        fail("update.err.no_release");
    } else if (status != 200) {
        ESP_LOGW(TAG, "GitHub answered %d", status);
        fail("update.err.network");
    } else {
        cJSON *root = cJSON_Parse(body);
        const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
        const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
        s_asset_url[0] = 0;
        s_asset_size = 0;
        const cJSON *a;
        cJSON_ArrayForEach(a, assets)
        {
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(a, "name");
            const cJSON *url = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
            const cJSON *size = cJSON_GetObjectItemCaseSensitive(a, "size");
            size_t nl = cJSON_IsString(name) ? strlen(name->valuestring) : 0;
            if (nl > 8 && strcmp(name->valuestring + nl - 8, "-ota.bin") == 0 && cJSON_IsString(url) &&
                strlen(url->valuestring) < sizeof(s_asset_url)) {
                strlcpy(s_asset_url, url->valuestring, sizeof(s_asset_url));
                s_asset_size = cJSON_IsNumber(size) ? size->valueint : 0;
            }
        }
        if (!cJSON_IsString(tag)) {
            fail("update.err.no_release");
        } else {
            const char *v = tag->valuestring[0] == 'v' || tag->valuestring[0] == 'V' ? tag->valuestring + 1 : tag->valuestring;
            strlcpy(s_latest, v, sizeof(s_latest));
            if (fwversion_compare(s_latest, APP_VERSION) <= 0) {
                s_state = UPD_UP_TO_DATE;
            } else if (!s_asset_url[0]) {
                fail("update.err.no_asset"); // newer release, but without an update file
            } else {
                s_state = UPD_AVAILABLE;
            }
            ESP_LOGI(TAG, "latest %s, this %s, update file %s", s_latest, APP_VERSION, s_asset_url[0] ? "yes" : "no");
        }
        cJSON_Delete(root);
    }
    free(body);
    vTaskDelete(NULL);
}

static void install_task(void *arg)
{
    esp_http_client_config_t http = {
        .url = s_asset_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
        .timeout_ms = 20000,
        .buffer_size = 8192,    // the download redirects to a host with long headers
        .buffer_size_tx = 4096, // and a long signed URL
        .keep_alive_enable = true,
    };
    // Erase the whole image area up front instead of piece by piece while downloading: on this board,
    // each flash erase disturbs the display (it flashes blue). The backlight stays off during the erase.
    esp_https_ota_config_t cfg = {.http_config = &http, .bulk_flash_erase = true};
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, &h);
    if (err == ESP_OK) {
        int total = esp_https_ota_get_image_size(h);
        if (total <= 0) total = s_asset_size;
        vTaskDelay(pdMS_TO_TICKS(1500)); // let the "screen turns off" message be read
        display_backlight(false);
        err = esp_https_ota_perform(h); // first call: erases the image area
        display_backlight(true);
        s_state = UPD_DOWNLOADING;
        while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int read = esp_https_ota_get_image_len_read(h);
            if (total > 0) s_progress = read * 100 / total;
            err = esp_https_ota_perform(h);
        }
        if (err == ESP_OK && !esp_https_ota_is_complete_data_received(h)) err = ESP_FAIL;
        if (err == ESP_OK) {
            err = esp_https_ota_finish(h);
        } else {
            esp_https_ota_abort(h);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(err));
        app_set_updating(false);
        fail(err == ESP_ERR_OTA_VALIDATE_FAILED ? "update.err.invalid" : "update.err.network");
        vTaskDelete(NULL);
        return;
    }
    s_progress = 100;
    s_state = UPD_DONE;
    ESP_LOGI(TAG, "installed %s, restarting", s_latest);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

bool updater_check_start(void)
{
    if (s_state == UPD_CHECKING || s_state == UPD_PREPARING || s_state == UPD_DOWNLOADING || s_state == UPD_DONE) return false;
    if (net_state() != NET_CONNECTED) {
        fail("update.err.no_wifi");
        return false;
    }
    s_state = UPD_CHECKING;
    if (xTaskCreate(check_task, "upd_check", 8192, NULL, 4, NULL) != pdPASS) {
        fail("update.err.network");
        return false;
    }
    return true;
}

bool updater_install_start(void)
{
    if (s_state != UPD_AVAILABLE) return false;
    app_status_t st;
    app_get_status(&st);
    if (st.recording) {
        fail("update.err.recording");
        return false;
    }
    app_set_updating(true); // no new recording: the device restarts at the end
    s_progress = 0;
    s_state = UPD_PREPARING;
    if (xTaskCreate(install_task, "upd_install", 8192, NULL, 4, NULL) != pdPASS) {
        app_set_updating(false);
        fail("update.err.network");
        return false;
    }
    return true;
}
