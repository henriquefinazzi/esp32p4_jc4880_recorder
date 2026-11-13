#include "settings_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

// ---------- Rules ----------

bool settings_host_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len == 0 || len > SETTINGS_HOST_MAX || s[0] == '-' || s[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

// Printable ASCII, no space (browsers do not send accents reliably in HTTP Basic).
static bool visible_ascii(const char *s, size_t min, size_t max, bool allow_colon)
{
    size_t len = s ? strlen(s) : 0;
    if (len < min || len > max) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x21 || c > 0x7E || (c == ':' && !allow_colon)) return false;
    }
    return true;
}

bool settings_user_valid(const char *s)
{
    return visible_ascii(s, 1, SETTINGS_USER_MAX, false);
}

bool settings_pass_valid(const char *s)
{
    return visible_ascii(s, SETTINGS_PASS_MIN, SETTINGS_PASS_MAX, true);
}

// Open network (empty password) or WPA: 8 to 63 characters, or the 64-digit hex key.
bool settings_wifi_valid(const char *ssid, const char *pass)
{
    size_t sl = ssid ? strlen(ssid) : 0, pl = pass ? strlen(pass) : 0;
    if (sl == 0 || sl > SETTINGS_SSID_MAX) return false;
    if (pl == 0) return true;
    if (pl >= 8 && pl <= 63) return true;
    if (pl != 64) return false;
    for (size_t i = 0; i < 64; i++) {
        char c = pass[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool settings_lang_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len < 2 || len > SETTINGS_LANG_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool settings_lock_pin_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len < SETTINGS_LOCK_PIN_MIN || len > SETTINGS_LOCK_PIN_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static bool lock_after_valid(int v)
{
    return v == 60 || v == 300 || v == 900;
}

bool settings_ntp_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len == 0 || len > SETTINGS_NTP_MAX || s[0] == '.' || s[0] == '-' || s[len - 1] == '.' || s[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
        if (c == '.' && s[i + 1] == '.') return false;
    }
    return true;
}

bool settings_ap_ssid_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len == 0 || len > SETTINGS_SSID_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) return false;
    }
    return true;
}

bool settings_ap_pass_valid(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len < SETTINGS_AP_PASS_MIN || len > SETTINGS_AP_PASS_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return false;
    }
    return true;
}

static bool brightness_valid(int v)
{
    return v >= SETTINGS_BRIGHTNESS_MIN && v <= 100;
}

static bool screen_off_valid(int v)
{
    return v == 0 || v == 10 || v == 30 || v == 60;
}

static bool gain_valid(int v)
{
    return v >= SETTINGS_GAIN_MIN_DB && v <= SETTINGS_GAIN_MAX_DB && v % SETTINGS_GAIN_STEP_DB == 0;
}

// ---------- Writing ----------

char *settings_to_json(const settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "versao", SETTINGS_VERSION);

    cJSON *dev = cJSON_AddObjectToObject(root, "aparelho");
    cJSON_AddNumberToObject(dev, "brilho", s->brightness);
    cJSON_AddNumberToObject(dev, "apagar_tela_s", s->screen_off_s);
    cJSON_AddNumberToObject(dev, "ganho_db", s->gain_db);
    cJSON_AddStringToObject(dev, "tema", s->theme ? "claro" : "escuro");
    cJSON_AddStringToObject(dev, "idioma", s->lang);
    static const char *const FORMATS[] = {"wav", "opus", "mp3"};
    cJSON_AddStringToObject(dev, "formato", FORMATS[s->rec_format >= 0 && s->rec_format <= 2 ? s->rec_format : 0]);
    cJSON_AddBoolToObject(dev, "filtro", s->filter);
    cJSON_AddBoolToObject(dev, "apagar_antigas", s->autodelete);

    cJSON *web = cJSON_AddObjectToObject(root, "navegador");
    cJSON_AddBoolToObject(web, "ligado", s->web_on);
    cJSON_AddStringToObject(web, "nome", s->host);
    cJSON_AddStringToObject(web, "usuario", s->user);
    cJSON_AddStringToObject(web, "senha", s->pass);
    cJSON_AddBoolToObject(web, "desenvolvedor", s->dev_on);

    cJSON *lock = cJSON_AddObjectToObject(root, "bloqueio");
    cJSON_AddBoolToObject(lock, "ligado", s->lock_on);
    cJSON_AddStringToObject(lock, "codigo", s->lock_pin);
    cJSON_AddNumberToObject(lock, "apos_s", s->lock_after_s);

    cJSON *clock = cJSON_AddObjectToObject(root, "relogio");
    cJSON_AddStringToObject(clock, "ntp", s->ntp);

    cJSON *ap = cJSON_AddObjectToObject(root, "rede_propria");
    cJSON_AddBoolToObject(ap, "ligada", s->ap_on);
    cJSON_AddStringToObject(ap, "nome", s->ap_ssid);
    cJSON_AddStringToObject(ap, "senha", s->ap_pass);

    cJSON *list = cJSON_AddArrayToObject(root, "wifi");
    for (int i = 0; i < s->wifi_count && i < SETTINGS_WIFI_MAX; i++) {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "rede", s->wifi[i].ssid);
        cJSON_AddStringToObject(n, "senha", s->wifi[i].pass);
        cJSON_AddItemToArray(list, n);
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

// ---------- Reading ----------

static void read_int(const cJSON *obj, const char *name, int *dst, bool (*valid)(int), int *ignored)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!it) return;
    double d = cJSON_IsNumber(it) ? it->valuedouble : -1e9;
    int v = (int)d;
    if (cJSON_IsNumber(it) && (double)v == d && valid(v)) {
        *dst = v;
    } else {
        (*ignored)++;
    }
}

static void read_str(const cJSON *obj, const char *name, char *dst, size_t len, bool (*valid)(const char *), int *ignored)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!it) return;
    if (cJSON_IsString(it) && strlen(it->valuestring) < len && valid(it->valuestring)) {
        strncpy(dst, it->valuestring, len - 1);
        dst[len - 1] = 0;
    } else {
        (*ignored)++;
    }
}

// The section's object, or NULL. A section that exists but is not an object counts as invalid.
static const cJSON *section(const cJSON *root, const char *name, int *ignored)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!it) return NULL;
    if (cJSON_IsObject(it)) return it;
    (*ignored)++;
    return NULL;
}

static void read_wifi(const cJSON *root, settings_t *out, int *ignored)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (!list) return;
    if (!cJSON_IsArray(list)) {
        (*ignored)++;
        return;
    }
    // The file's list replaces the device's; invalid or duplicate networks are left out.
    settings_t tmp = *out;
    tmp.wifi_count = 0;
    const cJSON *n;
    cJSON_ArrayForEach(n, list)
    {
        const cJSON *ss = cJSON_GetObjectItemCaseSensitive(n, "rede");
        const cJSON *pp = cJSON_GetObjectItemCaseSensitive(n, "senha");
        const char *ssid = cJSON_IsString(ss) ? ss->valuestring : NULL;
        const char *pass = pp == NULL ? "" : cJSON_IsString(pp) ? pp->valuestring : NULL;
        bool dup = false;
        for (int i = 0; ssid && i < tmp.wifi_count; i++) dup |= strcmp(tmp.wifi[i].ssid, ssid) == 0;
        if (!cJSON_IsObject(n) || !ssid || !pass || dup || !settings_wifi_valid(ssid, pass) || tmp.wifi_count >= SETTINGS_WIFI_MAX) {
            (*ignored)++;
            continue;
        }
        settings_wifi_t *w = &tmp.wifi[tmp.wifi_count++];
        snprintf(w->ssid, sizeof(w->ssid), "%s", ssid);
        snprintf(w->pass, sizeof(w->pass), "%s", pass);
    }
    out->wifi_count = tmp.wifi_count;
    memcpy(out->wifi, tmp.wifi, sizeof(out->wifi));
}

static bool bool_item(const cJSON *obj, const char *name, bool *dst, int *ignored)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!it) return false;
    if (cJSON_IsBool(it)) {
        *dst = cJSON_IsTrue(it);
        return true;
    }
    (*ignored)++;
    return false;
}

bool settings_from_json(const char *text, const settings_t *current, settings_t *out, int *ignored)
{
    *out = *current;
    *ignored = 0;
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *dev = section(root, "aparelho", ignored);
    if (dev) {
        read_int(dev, "brilho", &out->brightness, brightness_valid, ignored);
        read_int(dev, "apagar_tela_s", &out->screen_off_s, screen_off_valid, ignored);
        read_int(dev, "ganho_db", &out->gain_db, gain_valid, ignored);
        read_str(dev, "idioma", out->lang, sizeof(out->lang), settings_lang_valid, ignored);
        const cJSON *fm = cJSON_GetObjectItemCaseSensitive(dev, "formato");
        if (fm) {
            int f = !cJSON_IsString(fm) ? -1 : !strcmp(fm->valuestring, "wav") ? 0 : !strcmp(fm->valuestring, "opus") ? 1 : !strcmp(fm->valuestring, "mp3") ? 2 : -1;
            if (f >= 0) out->rec_format = f;
            else (*ignored)++;
        }
        bool_item(dev, "filtro", &out->filter, ignored);
        bool_item(dev, "apagar_antigas", &out->autodelete, ignored);
        const cJSON *th = cJSON_GetObjectItemCaseSensitive(dev, "tema");
        if (th) {
            if (cJSON_IsString(th) && strcmp(th->valuestring, "escuro") == 0) out->theme = 0;
            else if (cJSON_IsString(th) && strcmp(th->valuestring, "claro") == 0) out->theme = 1;
            else (*ignored)++;
        }
    }
    const cJSON *web = section(root, "navegador", ignored);
    if (web) {
        bool_item(web, "ligado", &out->web_on, ignored);
        bool_item(web, "desenvolvedor", &out->dev_on, ignored);
        read_str(web, "nome", out->host, sizeof(out->host), settings_host_valid, ignored);
        read_str(web, "usuario", out->user, sizeof(out->user), settings_user_valid, ignored);
        read_str(web, "senha", out->pass, sizeof(out->pass), settings_pass_valid, ignored);
    }
    const cJSON *lock = section(root, "bloqueio", ignored);
    if (lock) {
        read_str(lock, "codigo", out->lock_pin, sizeof(out->lock_pin), settings_lock_pin_valid, ignored);
        bool_item(lock, "ligado", &out->lock_on, ignored);
        read_int(lock, "apos_s", &out->lock_after_s, lock_after_valid, ignored);
        if (out->lock_on && !settings_lock_pin_valid(out->lock_pin)) out->lock_on = false; // no code, no lock
    }
    const cJSON *clock = section(root, "relogio", ignored);
    if (clock) read_str(clock, "ntp", out->ntp, sizeof(out->ntp), settings_ntp_valid, ignored);
    const cJSON *ap = section(root, "rede_propria", ignored);
    if (ap) {
        bool_item(ap, "ligada", &out->ap_on, ignored);
        read_str(ap, "nome", out->ap_ssid, sizeof(out->ap_ssid), settings_ap_ssid_valid, ignored);
        read_str(ap, "senha", out->ap_pass, sizeof(out->ap_pass), settings_ap_pass_valid, ignored);
    }
    read_wifi(root, out, ignored);

    cJSON_Delete(root);
    return true;
}

bool settings_equal(const settings_t *a, const settings_t *b)
{
    if (a->brightness != b->brightness || a->screen_off_s != b->screen_off_s || a->gain_db != b->gain_db || a->theme != b->theme || a->rec_format != b->rec_format || a->filter != b->filter || a->autodelete != b->autodelete || a->lock_on != b->lock_on || strcmp(a->lock_pin, b->lock_pin) || a->lock_after_s != b->lock_after_s || strcmp(a->lang, b->lang) || a->web_on != b->web_on || a->dev_on != b->dev_on) return false;
    if (strcmp(a->host, b->host) || strcmp(a->user, b->user) || strcmp(a->pass, b->pass)) return false;
    if (strcmp(a->ntp, b->ntp) || a->ap_on != b->ap_on || strcmp(a->ap_ssid, b->ap_ssid) || strcmp(a->ap_pass, b->ap_pass)) return false;
    if (a->wifi_count != b->wifi_count) return false;
    for (int i = 0; i < a->wifi_count; i++) {
        if (strcmp(a->wifi[i].ssid, b->wifi[i].ssid) || strcmp(a->wifi[i].pass, b->wifi[i].pass)) return false;
    }
    return true;
}
