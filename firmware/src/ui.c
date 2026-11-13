// Recorder UI (LVGL 9), 480x800 portrait.
// Recorder UI. Everything here runs in the LVGL task, except ui_init().
#include "ui.h"
#include "updater.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_idf_version.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app.h"
#include "config.h"
#include "display.h"
#include "fonts.h"
#include "i18n.h"
#include "net.h"
#include "web.h"

// 1 = at boot, walk through the screens and dump captures over serial (for review).
#define UI_TOUR 0

// Theme palettes. Screens use C_*; switching theme swaps s_pal and rebuilds everything.
typedef struct {
    uint32_t bg, surf, surf2, text, muted, green, red, amber, blue, key_fn;
    bool dark;
} palette_t;

static const palette_t PAL_DARK = {
    .bg = 0x0F1317, .surf = 0x1A2027, .surf2 = 0x242C35, .text = 0xE8ECEF, .muted = 0x8A949E,
    .green = 0x2FA24A, .red = 0xD63A3A, .amber = 0xE0A33A, .blue = 0x4A90E2, .key_fn = 0x33404D, .dark = true,
};
static const palette_t PAL_LIGHT = {
    .bg = 0xF2F4F6, .surf = 0xFFFFFF, .surf2 = 0xDCE2E8, .text = 0x161B21, .muted = 0x5E6973,
    .green = 0x1F8A3B, .red = 0xC62F2F, .amber = 0xA86400, .blue = 0x2E6DB4, .key_fn = 0xC5CDD5, .dark = false,
};
static const palette_t *s_pal = &PAL_DARK;
static int s_theme_applied = -1;

#define C_BG (s_pal->bg)
#define C_SURF (s_pal->surf)
#define C_SURF2 (s_pal->surf2)
#define C_TEXT (s_pal->text)
#define C_MUTED (s_pal->muted)
#define C_GREEN (s_pal->green)
#define C_RED (s_pal->red)
#define C_AMBER (s_pal->amber)
#define C_BLUE (s_pal->blue)
#define C_WHITE 0xFFFFFF
#define C_QR_DARK 0x0F1317 // QR always dark on white, in both themes

#define LEVEL_MIN_DB (-60)
#define TOAST_MS 8000
#define MAX_RECORDINGS 60

typedef enum { W_REGULAR, W_SEMIBOLD, W_BOLD } weight_t;
typedef enum { SCR_HOME, SCR_REC, SCR_LIST, SCR_DETAIL, SCR_RENAME, SCR_SETTINGS, SCR_WIFI, SCR_WIFI_PASS, SCR_NET_INFO, SCR_WEB, SCR_HOSTNAME, SCR_EDIT, SCR_AP, SCR_DEV, SCR_DISPLAY, SCR_LANG, SCR_RECCFG, SCR_ABOUT, SCR_PIN, SCR_CARD, SCR_NOTICE } screen_t;
typedef enum { NOTICE_USB, NOTICE_NO_CARD, NOTICE_UNREADABLE, NOTICE_CARD_FULL, NOTICE_WRITE_ERROR } notice_t;
typedef enum { BTN_PRIMARY, BTN_OUTLINE, BTN_NEUTRAL, BTN_DANGER, BTN_DANGER_FILLED } button_kind_t;

// ---------- UI state ----------

static lv_obj_t *s_root, *s_clock, *s_wifi_icon, *s_ap_icon, *s_content, *s_tabs;
static lv_obj_t *s_tab_items[3];
static screen_t s_cur = SCR_HOME;
static bool s_tour;

static uint32_t s_seen_event_seq;
static char s_toast_title[48], s_toast_sub[48];
static uint32_t s_toast_until;
static notice_t s_notice;
static uint32_t s_notice_seconds;

static app_recording_t s_recs[MAX_RECORDINGS];
static int s_rec_count;
static int s_sel_rec;
// Multi-selection of recordings in the list (to delete them at once).
static bool s_select_mode;
static bool s_selected[MAX_RECORDINGS];
static lv_obj_t *s_sel_marks[MAX_RECORDINGS];
static lv_obj_t *s_sel_btn_label;
static char s_sel_ssid[33];

static bool s_screen_off;
static lv_obj_t *s_wake_overlay;

// Pointers to things that change over time; cleared on every screen change.
static lv_obj_t *s_home_space, *s_rec_timer, *s_rec_bar, *s_rec_file, *s_rec_gain, *s_pass_ta;
static bool s_hostname_error; // the typed name was rejected
// SCR_EDIT: single text-field screen, reused for several settings.
typedef enum { EDIT_WEB_USER, EDIT_WEB_PASS, EDIT_AP_SSID, EDIT_AP_PASS, EDIT_NTP } edit_t;
static edit_t s_edit;
static bool s_lock_enable_after_pin; // when the PIN is saved, enable the lock
static bool s_edit_error;     // the typed value was rejected
static lv_obj_t *s_ap_clients; // connected-device counter on the own-network screen
static char s_home_web[64];   // address shown on the home screen ("" = none)
static lv_obj_t *s_keyboard; // child of the root, so it spans the full screen width
static bool s_home_card_ok, s_home_on_host, s_home_unreadable;
static bool s_wifi_scan_done;
static bool s_user_connect; // the ongoing connection was requested from the UI (failure shows a popup)
static net_state_t s_wifi_state;

static void show(screen_t s);
static void build_wifi(void);
static void edit_go_cb(lv_event_t *e);
static void apply_theme(void);

// ---------- Fonts ----------

static const lv_font_t *font(weight_t w, int size)
{
    static struct { weight_t w; int size; lv_font_t *f; } cache[20];
    static int n;
    for (int i = 0; i < n; i++) {
        if (cache[i].w == w && cache[i].size == size) return cache[i].f;
    }
    const uint8_t *data = w == W_BOLD ? montserrat_bold_ttf : w == W_SEMIBOLD ? montserrat_semibold_ttf : montserrat_regular_ttf;
    size_t len = w == W_BOLD ? montserrat_bold_ttf_size : w == W_SEMIBOLD ? montserrat_semibold_ttf_size : montserrat_regular_ttf_size;
    lv_font_t *f = lv_tiny_ttf_create_data(data, len, size);
    if (!f) return &lv_font_montserrat_20;
    if (n < (int)(sizeof(cache) / sizeof(cache[0]))) {
        cache[n].w = w;
        cache[n].size = size;
        cache[n].f = f;
        n++;
    }
    return f;
}

// Symbols (home, trash, Wi-Fi...) come from LVGL's built-in font.
static const lv_font_t *symbol_font(int size)
{
    return size >= 48 ? &lv_font_montserrat_48 : size >= 28 ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
}

// ---------- Building blocks ----------

static lv_obj_t *box(lv_obj_t *parent, lv_flex_flow_t flow, int gap)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_style_pad_row(o, gap, 0);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE); // otherwise an inner block swallows the row's tap
    return o;
}

static void bg(lv_obj_t *o, uint32_t color, lv_opa_t opa, int radius)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
}

static void pad(lv_obj_t *o, int hor, int ver)
{
    lv_obj_set_style_pad_hor(o, hor, 0);
    lv_obj_set_style_pad_ver(o, ver, 0);
}

static void center(lv_obj_t *o)
{
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void cross_center(lv_obj_t *o)
{
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static lv_obj_t *label(lv_obj_t *parent, const char *s, weight_t w, int size, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font(w, size), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *wrapped(lv_obj_t *parent, const char *s, weight_t w, int size, uint32_t color, bool centered)
{
    lv_obj_t *l = label(parent, s, w, size, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    if (centered) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *symbol(lv_obj_t *parent, const char *s, int size, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, symbol_font(size), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

static void on_click(lv_obj_t *o, lv_event_cb_t cb, void *user_data)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_style_opa(o, LV_OPA_60, LV_STATE_PRESSED);
}

static lv_obj_t *spacer(lv_obj_t *parent)
{
    lv_obj_t *o = box(parent, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_grow(o, 1);
    return o;
}

static lv_obj_t *card(lv_obj_t *parent, lv_flex_flow_t flow, int gap)
{
    lv_obj_t *o = box(parent, flow, gap);
    lv_obj_set_width(o, lv_pct(100));
    bg(o, C_SURF, LV_OPA_COVER, 14);
    pad(o, 16, 14);
    if (flow == LV_FLEX_FLOW_ROW) cross_center(o);
    return o;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, button_kind_t kind, const char *sym, lv_event_cb_t cb, void *ud)
{
    bool filled = kind == BTN_PRIMARY || kind == BTN_DANGER_FILLED;
    uint32_t color = kind == BTN_DANGER || kind == BTN_DANGER_FILLED ? C_RED : kind == BTN_NEUTRAL ? C_MUTED : C_GREEN;
    lv_obj_t *b = box(parent, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_width(b, lv_pct(100));
    pad(b, 16, 17);
    center(b);
    if (filled) {
        bg(b, color, LV_OPA_COVER, 16);
    } else {
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    }
    uint32_t fg = filled ? C_WHITE : color;
    if (sym) symbol(b, sym, 20, fg);
    label(b, text, W_SEMIBOLD, 20, fg);
    on_click(b, cb, ud);
    return b;
}

static lv_obj_t *big_button(lv_obj_t *parent, const char *text, const char *sym, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *b = box(parent, LV_FLEX_FLOW_COLUMN, 6);
    lv_obj_set_size(b, 240, 240);
    bg(b, color, LV_OPA_COVER, 120);
    center(b);
    symbol(b, sym, 48, C_WHITE);
    label(b, text, W_BOLD, 28, C_WHITE);
    on_click(b, cb, NULL);
    return b;
}

static void go_cb(lv_event_t *e)
{
    show((screen_t)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *header(const char *title, bool has_back, screen_t back_to)
{
    lv_obj_t *h = box(s_content, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_width(h, lv_pct(100));
    cross_center(h);
    if (has_back) {
        lv_obj_t *b = box(h, LV_FLEX_FLOW_ROW, 0);
        pad(b, 4, 6);
        symbol(b, LV_SYMBOL_LEFT, 28, C_TEXT);
        lv_obj_set_ext_click_area(b, 16);
        on_click(b, go_cb, (void *)(intptr_t)back_to);
    }
    label(h, title, W_BOLD, 26, C_TEXT);
    return h;
}

static void section(const char *s)
{
    label(s_content, s, W_SEMIBOLD, 14, C_MUTED);
}

// List row: title, value on the right and arrow (when it leads to another screen).
static lv_obj_t *row(const char *title, const char *sub, const char *value, const char *right_symbol, uint32_t symbol_color, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *r = card(s_content, LV_FLEX_FLOW_ROW, 12);
    lv_obj_t *left = box(r, LV_FLEX_FLOW_COLUMN, 3);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_t *t = label(left, title, W_SEMIBOLD, 18, C_TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, lv_pct(100));
    if (sub) label(left, sub, W_REGULAR, 14, C_MUTED);
    if (value) label(r, value, W_REGULAR, 16, C_MUTED);
    if (right_symbol) symbol(r, right_symbol, 20, symbol_color);
    if (cb) on_click(r, cb, ud);
    return r;
}

static void data_row(lv_obj_t *parent, const char *k, const char *v, uint32_t color)
{
    lv_obj_t *r = box(parent, LV_FLEX_FLOW_ROW, 8);
    lv_obj_set_width(r, lv_pct(100));
    pad(r, 0, 10);
    lv_obj_t *kl = label(r, k, W_REGULAR, 17, C_MUTED);
    lv_obj_set_flex_grow(kl, 1);
    label(r, v, W_SEMIBOLD, 17, color);
}

// ---------- Formatting ----------

static void fmt_duration(char *out, size_t len, uint32_t seconds)
{
    if (seconds >= 3600) {
        snprintf(out, len, "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
    } else {
        snprintf(out, len, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    }
}

static void fmt_gb(char *out, size_t len, uint32_t mb)
{
    snprintf(out, len, T("fmt.gb"), (unsigned long)(mb / 1024), (unsigned long)(mb % 1024 * 10 / 1024));
}

// File size: in KB below 1 MB, so short recordings don't show up as "0.0 MB".
static void fmt_mb(char *out, size_t len, uint32_t bytes)
{
    if (bytes < 1024 * 1024) {
        snprintf(out, len, "%lu KB", (unsigned long)((bytes + 512) / 1024));
        return;
    }
    uint32_t tenths = bytes / (1024 * 1024 / 10);
    snprintf(out, len, T("fmt.mb"), (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

// Hours of recording that fit (16 kHz mono WAV: 32 kB per second).
static uint32_t hours_for_mb(uint32_t mb)
{
    return (uint32_t)((uint64_t)mb * 1024 * 1024 / app_rec_bytes_per_s() / 3600);
}

// Month abbreviation (0 = January) in the current language.
static const char *month_abbr(int mon)
{
    static const char *const KEYS[12] = {"month.1", "month.2", "month.3", "month.4", "month.5", "month.6",
                                         "month.7", "month.8", "month.9", "month.10", "month.11", "month.12"};
    return T(KEYS[mon]);
}

// Recording date ("Today, 14:05", "Nov 7, 15:20"). false if recorded without a clock.
static bool fmt_recording_date(char *out, size_t len, const app_recording_t *r)
{
    if (!r->mtime) return false;
    time_t now = time(NULL);
    struct tm t, n;
    localtime_r(&r->mtime, &t);
    localtime_r(&now, &n);
    int days = (n.tm_year - t.tm_year) * 365 + (n.tm_yday - t.tm_yday);
    if (net_time_valid() && days == 0) {
        snprintf(out, len, T("date.today"), t.tm_hour, t.tm_min);
    } else if (net_time_valid() && days == 1) {
        snprintf(out, len, T("date.yesterday"), t.tm_hour, t.tm_min);
    } else {
        snprintf(out, len, T("date.day_month_time"), t.tm_mday, month_abbr(t.tm_mon), t.tm_hour, t.tm_min);
    }
    return true;
}

// How a recording is shown: its given name, else the date, else the number.
static void fmt_recording_title(char *out, size_t len, const app_recording_t *r)
{
    if (r->title[0]) {
        strlcpy(out, r->title, len);
    } else if (!fmt_recording_date(out, len, r)) {
        snprintf(out, len, T("rec.fallback_title"), r->id);
    }
}

static const char *timeout_text(int s)
{
    return s == 0 ? T("display.timeout.never") : s == 10 ? "10 s" : s == 30 ? "30 s" : "1 min";
}

// ---------- Confirmation dialog ----------

static lv_obj_t *s_dialog;
static void (*s_dialog_action)(void);

static void dialog_close(void)
{
    if (s_dialog) lv_obj_delete(s_dialog);
    s_dialog = NULL;
}

static void dialog_cancel_cb(lv_event_t *e)
{
    (void)e;
    dialog_close();
}

static void dialog_ok_cb(lv_event_t *e)
{
    (void)e;
    void (*action)(void) = s_dialog_action;
    dialog_close();
    if (action) action();
}

static void dialog_ex(const char *title, const char *text, const char *cancel_label, const char *ok_label,
                      button_kind_t ok_kind, const char *ok_symbol, void (*action)(void))
{
    dialog_close();
    s_dialog_action = action;
    s_dialog = box(s_root, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_add_flag(s_dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_dialog, LV_OBJ_FLAG_CLICKABLE); // blocks whatever is behind
    lv_obj_set_size(s_dialog, lv_pct(100), lv_pct(100));
    bg(s_dialog, 0x000000, LV_OPA_60, 0);
    pad(s_dialog, 24, 0);
    center(s_dialog);

    lv_obj_t *c = box(s_dialog, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_width(c, lv_pct(100));
    bg(c, C_SURF2, LV_OPA_COVER, 20);
    pad(c, 22, 22);
    wrapped(c, title, W_BOLD, 24, C_TEXT, false);
    wrapped(c, text, W_REGULAR, 17, C_MUTED, false);
    lv_obj_t *actions = box(c, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(actions, lv_pct(100));
    if (cancel_label) lv_obj_set_flex_grow(button(actions, cancel_label, BTN_NEUTRAL, NULL, dialog_cancel_cb, NULL), 1); // NULL: OK only
    lv_obj_set_flex_grow(button(actions, ok_label, ok_kind, ok_symbol, dialog_ok_cb, NULL), 1);
}

// Confirmation of a destructive action.
static void dialog(const char *title, const char *text, const char *ok_label, void (*action)(void))
{
    dialog_ex(title, text, T("common.cancel"), ok_label, BTN_DANGER_FILLED, LV_SYMBOL_TRASH, action);
}

// ---------- Full-screen notices ----------

static void retry_card_cb(lv_event_t *e)
{
    (void)e;
    app_retry_card();
    show(SCR_HOME);
}

static void format_unreadable_cb(lv_event_t *e);

static void notice(notice_t kind)
{
    const char *sym, *title, *btn = NULL;
    char text[160];
    uint32_t color;
    lv_event_cb_t cb = NULL;
    void *ud = NULL;
    char dur[16];
    fmt_duration(dur, sizeof(dur), s_notice_seconds);

    switch (kind) {
    case NOTICE_USB:
        sym = LV_SYMBOL_USB; color = C_BLUE; title = T("notice.usb.title");
        strlcpy(text, T("notice.usb.text"), sizeof(text));
        break;
    case NOTICE_NO_CARD:
        sym = LV_SYMBOL_SD_CARD; color = C_AMBER; title = T("notice.no_card.title");
        strlcpy(text, T("notice.no_card.text"), sizeof(text));
        btn = T("common.retry"); cb = retry_card_cb;
        break;
    case NOTICE_UNREADABLE:
        sym = LV_SYMBOL_SD_CARD; color = C_AMBER; title = T("notice.unreadable.title");
        strlcpy(text, T("notice.unreadable.text"), sizeof(text));
        btn = T("notice.unreadable.button"); cb = format_unreadable_cb;
        break;
    case NOTICE_CARD_FULL:
        sym = LV_SYMBOL_WARNING; color = C_RED; title = T("notice.card_full.title");
        snprintf(text, sizeof(text), T("notice.card_full.text"), dur);
        btn = T("notice.card_full.button"); cb = go_cb; ud = (void *)(intptr_t)SCR_LIST;
        break;
    default:
        sym = LV_SYMBOL_WARNING; color = C_RED; title = T("notice.write_error.title");
        snprintf(text, sizeof(text), T("notice.write_error.text"), dur);
        btn = T("notice.write_error.button"); cb = go_cb; ud = (void *)(intptr_t)SCR_HOME;
        break;
    }

    center(s_content);
    lv_obj_t *circle = box(s_content, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_size(circle, 120, 120);
    bg(circle, color, LV_OPA_20, 60);
    center(circle);
    symbol(circle, sym, 48, color);
    wrapped(s_content, title, W_BOLD, 28, C_TEXT, true);
    wrapped(s_content, text, W_REGULAR, 18, C_MUTED, true);
    if (btn) button(s_content, btn, kind == NOTICE_UNREADABLE ? BTN_DANGER : BTN_OUTLINE, kind == NOTICE_UNREADABLE ? LV_SYMBOL_TRASH : NULL, cb, ud);
    if (kind == NOTICE_UNREADABLE) button(s_content, T("common.retry"), BTN_NEUTRAL, NULL, retry_card_cb, NULL);
}

// ---------- Screens ----------

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    app_toggle_recording();
}

// Browser-access address for the home screen; "" when disabled or offline.
// Line 1: address; line 2: IP or network. "" when there is no way to connect.
#define HOME_SUB_LEN 48
static void home_web_text(char *out, size_t len, char sub[HOME_SUB_LEN])
{
    out[0] = 0;
    sub[0] = 0;
    if (!app_web_enabled()) return;
    if (app_ap_enabled()) {
        net_ap_info_t ai;
        net_ap_info(&ai);
        if (!ai.on || !ai.ip[0]) return;
        char ssid[APP_AP_SSID_MAX + 1];
        app_ap_ssid(ssid);
        snprintf(out, len, "http://%s", ai.ip);
        snprintf(sub, HOME_SUB_LEN, T("home.network"), ssid);
        return;
    }
    net_info_t info;
    if (net_state() != NET_CONNECTED || !net_info(&info) || !info.ip[0]) return;
    char host[APP_HOSTNAME_MAX + 1];
    app_hostname(host);
    snprintf(out, len, "http://%s.local", host);
    strlcpy(sub, info.ip, HOME_SUB_LEN);
}

static void build_home(void)
{
    app_status_t st;
    app_get_status(&st);
    s_home_card_ok = st.card_ok;
    s_home_unreadable = st.card_unreadable;
    s_home_on_host = st.card_on_host;

    if (st.card_on_host) {
        notice(NOTICE_USB);
        return;
    }
    if (st.card_unreadable) {
        notice(NOTICE_UNREADABLE);
        return;
    }
    if (!st.card_ok) {
        notice(NOTICE_NO_CARD);
        return;
    }

    center(s_content);
    lv_obj_set_style_pad_row(s_content, 20, 0);

    if (s_toast_title[0]) {
        lv_obj_t *t = card(s_content, LV_FLEX_FLOW_ROW, 12);
        bg(t, C_GREEN, LV_OPA_20, 14);
        symbol(t, LV_SYMBOL_OK, 20, C_GREEN);
        lv_obj_t *col = box(t, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_flex_grow(col, 1);
        label(col, s_toast_title, W_SEMIBOLD, 17, C_TEXT);
        label(col, s_toast_sub, W_REGULAR, 14, C_MUTED);
    } else {
        label(s_content, T("home.ready"), W_REGULAR, 22, C_MUTED);
    }

    big_button(s_content, T("home.record"), LV_SYMBOL_AUDIO, C_GREEN, toggle_cb);

    lv_obj_t *info = card(s_content, LV_FLEX_FLOW_ROW, 12);
    symbol(info, LV_SYMBOL_SD_CARD, 20, C_MUTED);
    s_home_space = label(info, "", W_REGULAR, 16, C_TEXT);

    // Browser access on and network up: address and IP; tapping opens the access screen.
    char ip[HOME_SUB_LEN];
    home_web_text(s_home_web, sizeof(s_home_web), ip);
    if (s_home_web[0]) {
        lv_obj_t *web = card(s_content, LV_FLEX_FLOW_ROW, 12);
        symbol(web, LV_SYMBOL_WIFI, 20, C_GREEN);
        lv_obj_t *col = box(web, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_t *addr = label(col, s_home_web, W_SEMIBOLD, 17, C_TEXT);
        lv_label_set_long_mode(addr, LV_LABEL_LONG_DOT);
        lv_obj_set_width(addr, lv_pct(100));
        label(col, ip, W_REGULAR, 15, C_MUTED);
        symbol(web, LV_SYMBOL_RIGHT, 20, C_MUTED);
        on_click(web, go_cb, (void *)(intptr_t)SCR_WEB);
    }
}

static void gain_cb(lv_event_t *e)
{
    app_status_t st;
    app_get_status(&st);
    app_set_gain_db(st.gain_db + (int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *gain_key(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *k = box(parent, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(k, 56, 50);
    bg(k, C_SURF2, LV_OPA_COVER, 12);
    center(k);
    symbol(k, sym, 20, C_TEXT);
    on_click(k, gain_cb, (void *)(intptr_t)delta);
    return k;
}

static void build_rec(void)
{
    center(s_content);
    lv_obj_set_style_pad_row(s_content, 16, 0);

    lv_obj_t *state = box(s_content, LV_FLEX_FLOW_ROW, 10);
    cross_center(state);
    lv_obj_t *dot = box(state, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(dot, 14, 14);
    bg(dot, C_RED, LV_OPA_COVER, 7);
    label(state, T("rec.recording"), W_BOLD, 22, C_RED);

    s_rec_timer = label(s_content, "00:00", W_BOLD, 96, C_TEXT);

    s_rec_bar = lv_bar_create(s_content);
    lv_obj_remove_style_all(s_rec_bar);
    lv_obj_set_size(s_rec_bar, lv_pct(100), 16);
    lv_bar_set_range(s_rec_bar, LEVEL_MIN_DB, 0);
    lv_obj_set_style_bg_color(s_rec_bar, lv_color_hex(C_SURF2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_rec_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_rec_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rec_bar, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_rec_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_rec_bar, 8, LV_PART_INDICATOR);

    s_rec_file = label(s_content, "", W_REGULAR, 16, C_MUTED);

    lv_obj_t *gain = card(s_content, LV_FLEX_FLOW_ROW, 8);
    pad(gain, 16, 8);
    lv_obj_set_style_pad_right(gain, 8, 0);
    lv_obj_t *gl = label(gain, T("rec.gain"), W_SEMIBOLD, 17, C_TEXT);
    lv_obj_set_flex_grow(gl, 1);
    gain_key(gain, LV_SYMBOL_MINUS, -APP_GAIN_STEP_DB);
    lv_obj_t *val = box(gain, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(val, 78, 50);
    center(val);
    s_rec_gain = label(val, "", W_SEMIBOLD, 20, C_TEXT);
    gain_key(gain, LV_SYMBOL_PLUS, APP_GAIN_STEP_DB);

    big_button(s_content, T("rec.stop"), LV_SYMBOL_STOP, C_RED, toggle_cb);

    int to = app_screen_timeout_s();
    if (to > 0) {
        char hint[64];
        snprintf(hint, sizeof(hint), T("rec.screen_off_hint"), timeout_text(to));
        wrapped(s_content, hint, W_REGULAR, 15, C_MUTED, true);
    }
}

static int selected_count(void)
{
    int n = 0;
    for (int i = 0; i < s_rec_count; i++) n += s_selected[i];
    return n;
}

static void update_select_button(void)
{
    if (!s_sel_btn_label) return;
    char t[40];
    snprintf(t, sizeof(t), T("list.delete_n"), selected_count());
    lv_label_set_text(s_sel_btn_label, t);
}

static void rec_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_select_mode) {
        s_selected[i] = !s_selected[i];
        if (s_sel_marks[i]) lv_label_set_text(s_sel_marks[i], s_selected[i] ? LV_SYMBOL_OK : " ");
        update_select_button();
        return;
    }
    s_sel_rec = i;
    show(SCR_DETAIL);
}

static void select_mode_cb(lv_event_t *e)
{
    (void)e;
    s_select_mode = !s_select_mode;
    memset(s_selected, 0, sizeof(s_selected));
    show(SCR_LIST);
}

static void delete_many_action(void)
{
    int failed = 0;
    for (int i = 0; i < s_rec_count; i++) {
        if (s_selected[i] && !app_delete_recording(s_recs[i].id)) failed++;
    }
    s_select_mode = false;
    memset(s_selected, 0, sizeof(s_selected));
    show(SCR_LIST);
    if (failed) dialog_ex(T("list.delete_many.fail_title"), T("list.delete_many.fail_text"), NULL, T("common.ok"), BTN_PRIMARY, NULL, NULL);
}

static void delete_many_cb(lv_event_t *e)
{
    (void)e;
    int n = selected_count();
    if (n == 0) return;
    char title[64];
    snprintf(title, sizeof(title), T(n == 1 ? "list.delete_many.title_one" : "list.delete_many.title"), n);
    dialog(title, T("list.delete_many.text"), T("common.delete"), delete_many_action);
}

static void build_list(void)
{
    lv_obj_t *h = header(T("list.title"), false, SCR_HOME);
    s_rec_count = app_list_recordings(s_recs, MAX_RECORDINGS);
    memset(s_sel_marks, 0, sizeof(s_sel_marks));
    s_sel_btn_label = NULL;
    if (s_rec_count == 0) {
        s_select_mode = false;
        wrapped(s_content, T("list.empty"), W_REGULAR, 18, C_MUTED, false);
        return;
    }
    // Header button: enters and leaves selection mode.
    spacer(h);
    lv_obj_t *sel = box(h, LV_FLEX_FLOW_ROW, 0);
    bg(sel, C_SURF, LV_OPA_COVER, 12);
    pad(sel, 14, 10);
    label(sel, T(s_select_mode ? "common.cancel" : "list.select"), W_SEMIBOLD, 16, s_select_mode ? C_TEXT : C_GREEN);
    on_click(sel, select_mode_cb, NULL);
    if (s_select_mode) {
        lv_obj_t *b = button(s_content, "", BTN_DANGER, LV_SYMBOL_TRASH, delete_many_cb, NULL);
        s_sel_btn_label = lv_obj_get_child(b, lv_obj_get_child_count(b) - 1);
        update_select_button();
    }
    for (int i = 0; i < s_rec_count; i++) {
        char title[APP_TITLE_MAX + 1], date[32], dur[16], size[16], sub[80];
        fmt_recording_title(title, sizeof(title), &s_recs[i]);
        fmt_duration(dur, sizeof(dur), s_recs[i].seconds);
        fmt_mb(size, sizeof(size), s_recs[i].bytes);
        if (s_recs[i].title[0] && fmt_recording_date(date, sizeof(date), &s_recs[i])) {
            snprintf(sub, sizeof(sub), "%s · %s · %s", date, dur, size); // with a name, the date moves to the second line
        } else {
            snprintf(sub, sizeof(sub), "%s · %s", dur, size);
        }
        lv_obj_t *r = row(title, sub, NULL, s_select_mode ? (s_selected[i] ? LV_SYMBOL_OK : " ") : LV_SYMBOL_RIGHT,
                          s_select_mode ? C_GREEN : C_MUTED, rec_row_cb, (void *)(intptr_t)i);
        if (s_select_mode) {
            s_sel_marks[i] = lv_obj_get_child(r, lv_obj_get_child_count(r) - 1);
            lv_obj_set_width(s_sel_marks[i], 24);
        }
    }
}

static void delete_action(void)
{
    if (s_sel_rec < s_rec_count) app_delete_recording(s_recs[s_sel_rec].id);
    show(SCR_LIST);
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    char title[APP_TITLE_MAX + 1], dur[16], text[160];
    fmt_recording_title(title, sizeof(title), &s_recs[s_sel_rec]);
    fmt_duration(dur, sizeof(dur), s_recs[s_sel_rec].seconds);
    snprintf(text, sizeof(text), T("detail.delete.text"), title, dur);
    dialog(T("detail.delete.title"), text, T("common.delete"), delete_action);
}

static void build_detail(void)
{
    header(T("detail.title"), true, SCR_LIST);
    if (s_sel_rec >= s_rec_count) return;
    const app_recording_t *r = &s_recs[s_sel_rec];
    char title[APP_TITLE_MAX + 1], date[32], dur[16], size[16], file[16];
    fmt_recording_title(title, sizeof(title), r);
    fmt_duration(dur, sizeof(dur), r->seconds);
    fmt_mb(size, sizeof(size), r->bytes);
    snprintf(file, sizeof(file), "REC_%04d", r->id);

    wrapped(s_content, title, W_BOLD, 30, C_TEXT, false);
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 6);
    if (fmt_recording_date(date, sizeof(date), r)) data_row(c, T("detail.date"), date, C_TEXT);
    data_row(c, T("detail.duration"), dur, C_TEXT);
    data_row(c, T("detail.size"), size, C_TEXT);
    data_row(c, T("detail.number"), file, C_TEXT);
    spacer(s_content);
    button(s_content, r->title[0] ? T("detail.rename") : T("detail.name"), BTN_OUTLINE, LV_SYMBOL_EDIT, go_cb, (void *)(intptr_t)SCR_RENAME);
    button(s_content, T("common.delete"), BTN_DANGER, LV_SYMBOL_TRASH, delete_cb, NULL);
}

static void timeout_cb(lv_event_t *e)
{
    (void)e;
    int s = app_screen_timeout_s();
    app_set_screen_timeout_s(s == 10 ? 30 : s == 30 ? 60 : s == 60 ? 0 : 10);
    show(s_cur);
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    // Applies while the finger drags; only saved to storage on release.
    app_set_brightness(v, lv_event_get_code(e) == LV_EVENT_RELEASED);
    lv_label_set_text_fmt((lv_obj_t *)lv_event_get_user_data(e), "%d%%", v);
}

static void brightness_row(void)
{
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", app_brightness());
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_t *top = box(c, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(top, lv_pct(100));
    lv_obj_t *t = label(top, T("display.brightness"), W_SEMIBOLD, 18, C_TEXT);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_t *value = label(top, pct, W_REGULAR, 16, C_MUTED);

    lv_obj_t *track = box(c, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_width(track, lv_pct(100));
    pad(track, 16, 12); // the knob (28 px) is larger than the track: without this padding the block clips it
    lv_obj_t *s = lv_slider_create(track);
    lv_obj_set_size(s, lv_pct(100), 10);
    lv_slider_set_range(s, DISPLAY_BRIGHTNESS_MIN, 100);
    lv_slider_set_value(s, app_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_SURF2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(s_pal->dark ? C_WHITE : C_GREEN), LV_PART_KNOB); // white disappears on the light theme
    lv_obj_set_style_pad_all(s, 9, LV_PART_KNOB); // 28 px knob
    lv_obj_set_ext_click_area(s, 24);              // touch area larger than the track
    lv_obj_add_event_cb(s, brightness_cb, LV_EVENT_VALUE_CHANGED, value);
    lv_obj_add_event_cb(s, brightness_cb, LV_EVENT_RELEASED, value);
}

static void theme_cb(lv_event_t *e)
{
    app_set_theme((int)(intptr_t)lv_event_get_user_data(e)); // the tick rebuilds the UI in the new theme
}

// Settings > Display: brightness, screen-off timeout and theme.
static void switch_row(const char *title, const char *sub, bool on, lv_event_cb_t cb);

static void lock_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (on && !app_set_lock_enabled(true)) {
        // No PIN yet: ask for one and enable on save.
        s_lock_enable_after_pin = true;
        show(SCR_PIN);
        return;
    }
    if (!on) app_set_lock_enabled(false);
    show(SCR_DISPLAY);
}

static void lock_after_cb(lv_event_t *e)
{
    (void)e;
    int s = app_lock_after_s();
    app_set_lock_after_s(s == 60 ? 300 : s == 300 ? 900 : 60);
    show(SCR_DISPLAY);
}

static void build_display(void)
{
    header(T("display.title"), true, SCR_SETTINGS);
    brightness_row();
    row(T("display.timeout"), T("display.timeout.sub"), timeout_text(app_screen_timeout_s()), LV_SYMBOL_RIGHT, C_MUTED, timeout_cb, NULL);
    section(T("lock.section"));
    switch_row(T("lock.enable"), T("lock.enable_sub"), app_lock_enabled(), lock_sw_cb);
    if (app_lock_enabled()) {
        char pin[APP_LOCK_PIN_MAX + 1], dots[3 * APP_LOCK_PIN_MAX + 1] = "";
        app_lock_pin(pin);
        for (size_t i = 0; i < strlen(pin); i++) strlcat(dots, "•", sizeof(dots));
        row(T("lock.code"), NULL, dots, LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_PIN);
        char after[24];
        snprintf(after, sizeof(after), T("lock.after_min"), app_lock_after_s() / 60);
        row(T("lock.after"), NULL, after, LV_SYMBOL_RIGHT, C_MUTED, lock_after_cb, NULL);
    }
    section(T("display.theme"));
    lv_obj_t *opts = box(s_content, LV_FLEX_FLOW_ROW, 12);
    lv_obj_set_width(opts, lv_pct(100));
    static const char *const NAMES[2] = {"display.theme.dark", "display.theme.light"}; // text keys
    static const palette_t *const PALS[2] = {&PAL_DARK, &PAL_LIGHT};
    for (int i = 0; i < 2; i++) {
        bool sel = app_theme() == i;
        lv_obj_t *o = box(opts, LV_FLEX_FLOW_COLUMN, 10);
        lv_obj_set_flex_grow(o, 1);
        bg(o, C_SURF, LV_OPA_COVER, 14);
        pad(o, 14, 14);
        lv_obj_set_style_border_width(o, sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(C_GREEN), 0);
        // Theme thumbnail: background, a card and a button.
        lv_obj_t *mini = box(o, LV_FLEX_FLOW_COLUMN, 6);
        lv_obj_set_width(mini, lv_pct(100));
        bg(mini, PALS[i]->bg, LV_OPA_COVER, 8);
        pad(mini, 10, 10);
        lv_obj_set_style_border_width(mini, 1, 0);
        lv_obj_set_style_border_color(mini, lv_color_hex(C_SURF2), 0);
        lv_obj_t *bar = box(mini, LV_FLEX_FLOW_ROW, 0);
        lv_obj_set_size(bar, lv_pct(100), 18);
        bg(bar, PALS[i]->surf, LV_OPA_COVER, 5);
        lv_obj_t *bar2 = box(mini, LV_FLEX_FLOW_ROW, 0);
        lv_obj_set_size(bar2, lv_pct(60), 18);
        bg(bar2, PALS[i]->green, LV_OPA_COVER, 5);
        lv_obj_t *nm = box(o, LV_FLEX_FLOW_ROW, 8);
        cross_center(nm);
        symbol(nm, sel ? LV_SYMBOL_OK : " ", 20, C_GREEN);
        label(nm, T(NAMES[i]), W_SEMIBOLD, 17, C_TEXT);
        on_click(o, theme_cb, (void *)(intptr_t)i);
    }
}

// Settings > Recording: file format, filter and what to do when the card is full.
static void fmt_cb(lv_event_t *e)
{
    app_set_rec_format((int)(intptr_t)lv_event_get_user_data(e));
    show(SCR_RECCFG);
}

static void filter_sw_cb(lv_event_t *e)
{
    app_set_filter_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void autodel_sw_cb(lv_event_t *e)
{
    app_set_autodelete_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void switch_row(const char *title, const char *sub, bool on, lv_event_cb_t cb)
{
    lv_obj_t *r = card(s_content, LV_FLEX_FLOW_ROW, 12);
    lv_obj_t *left = box(r, LV_FLEX_FLOW_COLUMN, 3);
    lv_obj_set_flex_grow(left, 1);
    label(left, title, W_SEMIBOLD, 18, C_TEXT);
    wrapped(left, sub, W_REGULAR, 14, C_MUTED, false);
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 64, 36);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_reccfg(void)
{
    header(T("reccfg.title"), true, SCR_SETTINGS);
    section(T("reccfg.format"));
    static const char *const NAMES[] = {"WAV", "Opus", "MP3"};
    static const char *const SUBS[] = {"reccfg.wav", "reccfg.opus", "reccfg.mp3"};
    for (int i = 0; i < 3; i++) {
        bool sel = app_rec_format() == i;
        row(NAMES[i], T(SUBS[i]), NULL, sel ? LV_SYMBOL_OK : NULL, C_GREEN, fmt_cb, (void *)(intptr_t)i);
    }
    wrapped(s_content, T("reccfg.format_note"), W_REGULAR, 14, C_MUTED, false);
    section(T("reccfg.audio"));
    switch_row(T("reccfg.filter"), T("reccfg.filter_sub"), app_filter_enabled(), filter_sw_cb);
    section(T("reccfg.card"));
    switch_row(T("reccfg.autodel"), T("reccfg.autodel_sub"), app_autodelete_enabled(), autodel_sw_cb);
}

// Settings > Language: built-in ones plus those in the card's LANGUAGES folder.
static i18n_lang_t s_langs[I18N_MAX_LANGS];

static void lang_cb(lv_event_t *e)
{
    app_set_language(s_langs[(intptr_t)lv_event_get_user_data(e)].code); // the tick switches and rebuilds
}

static void build_lang(void)
{
    header(T("lang.title"), true, SCR_SETTINGS);
    int n = i18n_list(s_langs, I18N_MAX_LANGS);
    for (int i = 0; i < n; i++) {
        bool sel = strcmp(s_langs[i].code, i18n_current()) == 0;
        row(s_langs[i].name, s_langs[i].builtin ? NULL : T("lang.card"), NULL, sel ? LV_SYMBOL_OK : NULL, C_GREEN, lang_cb, (void *)(intptr_t)i);
    }
    wrapped(s_content, T("lang.footer"), W_REGULAR, 14, C_MUTED, false);
}

// ---------- About ----------

static lv_obj_t *s_about_progress;
static lv_obj_t *s_about_update;      // status line of the firmware update row
static upd_state_t s_about_update_state;

static void update_text(char *out, size_t len)
{
    switch (updater_state()) {
    case UPD_CHECKING: strlcpy(out, T("update.checking"), len); break;
    case UPD_UP_TO_DATE: strlcpy(out, T("update.up_to_date"), len); break;
    case UPD_AVAILABLE: snprintf(out, len, T("update.available"), updater_latest()); break;
    case UPD_PREPARING: strlcpy(out, T("update.preparing"), len); break;
    case UPD_DOWNLOADING: snprintf(out, len, T("update.downloading"), updater_progress()); break;
    case UPD_DONE: strlcpy(out, T("update.done"), len); break;
    case UPD_ERROR: strlcpy(out, T(updater_error_key()), len); break;
    default: strlcpy(out, T("update.idle"), len); break;
    }
}

static void update_install_action(void)
{
    updater_install_start();
    show(SCR_ABOUT);
}

static void update_cb(lv_event_t *e)
{
    (void)e;
    if (updater_state() == UPD_AVAILABLE) {
        char title[64];
        snprintf(title, sizeof(title), T("update.confirm.title"), updater_latest());
        dialog_ex(title, T("update.confirm.text"), T("common.cancel"), T("update.install"), BTN_PRIMARY, NULL, update_install_action);
        return;
    }
    updater_check_start();
    show(SCR_ABOUT);
}

static void c6_update_action(void)
{
    app_status_t st;
    app_get_status(&st);
    if (st.recording || !net_wifi_fw_update_start()) {
        dialog_ex(T("about.c6.busy_title"), T("about.c6.busy_text"), NULL, T("common.ok"), BTN_PRIMARY, NULL, NULL);
        return;
    }
    app_set_updating(true); // prevents starting a recording: the device reboots at the end
    show(SCR_ABOUT);
}

static void c6_update_cb(lv_event_t *e)
{
    (void)e;
    dialog_ex(T("about.c6.confirm_title"), T("about.c6.confirm_text"), T("common.cancel"), T("about.c6.update_short"), BTN_PRIMARY, NULL,
              c6_update_action);
}

static void fmt_uptime(char *out, size_t len)
{
    uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000);
    if (s >= 86400) snprintf(out, len, T("about.uptime.days"), (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24));
    else if (s >= 3600) snprintf(out, len, T("about.uptime.hours"), (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    else snprintf(out, len, T("about.uptime.min"), (unsigned long)(s / 60));
}

static const char *reset_text(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return T("reset.poweron");
    case ESP_RST_SW: return T("reset.sw");
    case ESP_RST_PANIC: return T("reset.panic");
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return T("reset.wdt");
    case ESP_RST_BROWNOUT: return T("reset.brownout");
    case ESP_RST_USB: case ESP_RST_EXT: return T("reset.usb");
    default: return T("reset.other");
    }
}

#define PROJECT_URL_SHORT "github.com/henriquefinazzi/esp32p4_jc4880_recorder"
extern const lv_image_dsc_t logo_dark_ink, logo_light_ink;

static void build_about(void)
{
    header(T("about.title"), true, SCR_SETTINGS);

    const esp_app_desc_t *d = esp_app_get_description();
    char fw[48], c6[24], idf[24], chip[40], mem[40], up[32], mac[24] = "—";
    snprintf(fw, sizeof(fw), "%s · %.6s", APP_VERSION, d->date);
    bool c6_ok = net_wifi_fw_version(c6);
    if (!c6_ok) strlcpy(c6, T("about.c6.no_answer"), sizeof(c6));
    snprintf(idf, sizeof(idf), "%s", esp_get_idf_version());
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    snprintf(chip, sizeof(chip), "ESP32-P4 rev %d.%d", ci.revision / 100, ci.revision % 100);
    snprintf(mem, sizeof(mem), "%u KB + %u MB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / (1024 * 1024)));
    fmt_uptime(up, sizeof(up));
    net_info_t ni;
    if (net_info(&ni)) strlcpy(mac, ni.mac, sizeof(mac));
    const esp_partition_t *part = esp_ota_get_running_partition();

    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 4);
    data_row(c, T("about.firmware"), fw, C_TEXT);
    data_row(c, T("about.c6"), c6, C_TEXT);
    data_row(c, T("about.mac"), mac, C_TEXT);
    data_row(c, T("about.chip"), chip, C_TEXT);
    data_row(c, "ESP-IDF", idf, C_TEXT);
    data_row(c, T("about.partition"), part ? part->label : "?", C_TEXT);
    data_row(c, T("about.memory"), mem, C_TEXT);
    data_row(c, T("about.uptime"), up, C_TEXT);
    data_row(c, T("about.reset"), reset_text(), C_TEXT);

    // Firmware update from the project's GitHub releases.
    char upd[96];
    update_text(upd, sizeof(upd));
    upd_state_t us = updater_state();
    bool actionable = us == UPD_IDLE || us == UPD_UP_TO_DATE || us == UPD_ERROR || us == UPD_AVAILABLE;
    lv_obj_t *ur = row(T("update.title"), upd, NULL, us == UPD_AVAILABLE ? LV_SYMBOL_DOWNLOAD : actionable ? LV_SYMBOL_REFRESH : NULL,
                       us == UPD_AVAILABLE ? C_GREEN : C_MUTED, actionable ? update_cb : NULL, NULL);
    s_about_update = lv_obj_get_child(lv_obj_get_child(ur, 0), 1); // the subtitle label
    s_about_update_state = us;

    // Author logo (ink matching the theme) and the project's source code address.
    lv_obj_t *src = card(s_content, LV_FLEX_FLOW_COLUMN, 6);
    lv_obj_set_flex_align(src, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pad(src, 16, 12);
    lv_obj_t *logo = lv_image_create(src);
    lv_image_set_src(logo, s_pal->dark ? &logo_light_ink : &logo_dark_ink);
    lv_image_set_scale(logo, 192); // 75%: 225 x 68
    lv_obj_set_size(logo, 225, 68);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CENTER);
    label(src, PROJECT_URL_SHORT, W_REGULAR, 14, C_MUTED);

    int p = net_wifi_fw_update_progress();
    s_about_progress = NULL;
    if (p >= 0 || p == -2) {
        s_about_progress = wrapped(s_content, "", W_SEMIBOLD, 16, p == -2 ? C_RED : C_GREEN, true);
    } else if (c6_ok && strcmp(c6, net_wifi_fw_bundled()) != 0) {
        char t[64];
        snprintf(t, sizeof(t), T("about.c6.update"), net_wifi_fw_bundled());
        button(s_content, t, BTN_OUTLINE, LV_SYMBOL_DOWNLOAD, c6_update_cb, NULL);
        wrapped(s_content, T("about.c6.update_sub"), W_REGULAR, 14, C_MUTED, false);
    }
}

// ---------- Screen lock ----------

static lv_obj_t *s_lock, *s_lock_time, *s_lock_rec, *s_lock_dots, *s_lock_msg;
static bool s_locked;
static char s_lock_entry[APP_LOCK_PIN_MAX + 1];
static int s_lock_fails;
static uint32_t s_lock_wait_until;

#define LOCK_MAX_FAILS 5
#define LOCK_WAIT_MS 30000

static void lock_update_dots(void)
{
    char d[3 * APP_LOCK_PIN_MAX + 2 * APP_LOCK_PIN_MAX + 1] = "";
    for (size_t i = 0; i < strlen(s_lock_entry); i++) strlcat(d, i ? " •" : "•", sizeof(d));
    lv_label_set_text(s_lock_dots, d[0] ? d : " ");
}

static void lock_hide(void)
{
    s_locked = false;
    if (s_lock) lv_obj_delete(s_lock);
    s_lock = NULL;
    lv_display_trigger_activity(NULL);
}

static void lock_key_cb(lv_event_t *e)
{
    const char *k = lv_event_get_user_data(e);
    if ((int32_t)(s_lock_wait_until - lv_tick_get()) > 0) return; // waiting after too many failed attempts
    size_t n = strlen(s_lock_entry);
    if (strcmp(k, "<") == 0) {
        if (n) s_lock_entry[n - 1] = 0;
    } else if (n < APP_LOCK_PIN_MAX) {
        s_lock_entry[n] = k[0];
        s_lock_entry[n + 1] = 0;
    }
    lv_label_set_text(s_lock_msg, T("lock.enter"));
    lv_obj_set_style_text_color(s_lock_msg, lv_color_hex(C_MUTED), 0);
    char pin[APP_LOCK_PIN_MAX + 1];
    app_lock_pin(pin);
    if (strlen(s_lock_entry) == strlen(pin)) {
        if (strcmp(s_lock_entry, pin) == 0) {
            s_lock_fails = 0;
            s_lock_entry[0] = 0;
            lock_hide();
            return;
        }
        s_lock_entry[0] = 0;
        if (++s_lock_fails >= LOCK_MAX_FAILS) {
            s_lock_fails = 0;
            s_lock_wait_until = lv_tick_get() + LOCK_WAIT_MS;
        }
        lv_label_set_text(s_lock_msg, T("lock.wrong"));
        lv_obj_set_style_text_color(s_lock_msg, lv_color_hex(C_RED), 0);
    }
    lock_update_dots();
}

// Large numeric keypad (3 x 4). Keys: "0"-"9", "<" deletes and, with `with_ok`, ">" confirms.
static void keypad(lv_obj_t *parent, lv_event_cb_t cb, bool with_ok)
{
    static const char *const KEYS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "<"};
    static const char *const OK = ">";
    for (int r = 0; r < 4; r++) {
        lv_obj_t *rowb = box(parent, LV_FLEX_FLOW_ROW, 18);
        for (int c = 0; c < 3; c++) {
            const char *k = KEYS[r * 3 + c];
            if (!k[0] && with_ok) k = OK;
            lv_obj_t *b = box(rowb, LV_FLEX_FLOW_COLUMN, 0);
            lv_obj_set_size(b, 96, 72);
            center(b);
            if (!k[0]) continue; // empty corner
            bg(b, k == OK ? C_GREEN : C_SURF, LV_OPA_COVER, 36);
            if (k[0] == '<') symbol(b, LV_SYMBOL_BACKSPACE, 28, C_TEXT);
            else if (k == OK) symbol(b, LV_SYMBOL_OK, 28, C_WHITE);
            else label(b, k, W_SEMIBOLD, 30, C_TEXT);
            on_click(b, cb, (void *)k);
            lv_obj_set_style_bg_color(b, lv_color_hex(k == OK ? C_TEXT : C_GREEN), LV_STATE_PRESSED);
        }
    }
}

// ---------- Set the lock PIN (digits only, entered twice) ----------

static int s_pin_step;            // 0: new PIN; 1: confirmation
static char s_pin_first[APP_LOCK_PIN_MAX + 1], s_pin_entry[APP_LOCK_PIN_MAX + 1];
static lv_obj_t *s_pin_dots, *s_pin_msg, *s_pin_prompt;

static void pin_refresh(const char *msg_key, bool error)
{
    char d[5 * APP_LOCK_PIN_MAX + 1] = "";
    for (size_t i = 0; i < strlen(s_pin_entry); i++) strlcat(d, i ? " •" : "•", sizeof(d));
    lv_label_set_text(s_pin_dots, d[0] ? d : " ");
    lv_label_set_text(s_pin_prompt, T(s_pin_step ? "pin.confirm" : "pin.new"));
    lv_label_set_text(s_pin_msg, msg_key ? T(msg_key) : T("edit.lock_pin.rule"));
    lv_obj_set_style_text_color(s_pin_msg, lv_color_hex(error ? C_RED : C_MUTED), 0);
}

static void pin_key_cb(lv_event_t *e)
{
    const char *k = lv_event_get_user_data(e);
    size_t n = strlen(s_pin_entry);
    if (k[0] == '<') {
        if (n) s_pin_entry[n - 1] = 0;
        pin_refresh(NULL, false);
        return;
    }
    if (k[0] != '>') {
        if (n < APP_LOCK_PIN_MAX) {
            s_pin_entry[n] = k[0];
            s_pin_entry[n + 1] = 0;
        }
        pin_refresh(NULL, false);
        return;
    }
    // Confirm
    if (n < 4) {
        pin_refresh("pin.short", true);
        return;
    }
    if (s_pin_step == 0) {
        strlcpy(s_pin_first, s_pin_entry, sizeof(s_pin_first));
        s_pin_entry[0] = 0;
        s_pin_step = 1;
        pin_refresh(NULL, false);
        return;
    }
    if (strcmp(s_pin_first, s_pin_entry) != 0) {
        s_pin_step = 0;
        s_pin_first[0] = s_pin_entry[0] = 0;
        pin_refresh("pin.mismatch", true);
        return;
    }
    app_set_lock_pin(s_pin_entry);
    if (s_lock_enable_after_pin) app_set_lock_enabled(true); // lock was enabled before a PIN existed
    s_lock_enable_after_pin = false;
    s_pin_first[0] = s_pin_entry[0] = 0;
    show(SCR_DISPLAY);
}

static void build_pin(void)
{
    header(T("edit.lock_pin.title"), true, SCR_DISPLAY);
    s_pin_step = 0;
    s_pin_first[0] = s_pin_entry[0] = 0;
    lv_obj_t *c = box(s_content, LV_FLEX_FLOW_COLUMN, 10);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pad(c, 0, 16);
    s_pin_prompt = label(c, "", W_SEMIBOLD, 20, C_TEXT);
    s_pin_dots = label(c, " ", W_BOLD, 28, C_TEXT);
    s_pin_msg = label(c, "", W_REGULAR, 15, C_MUTED);
    lv_obj_t *sp = box(c, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_height(sp, 6);
    keypad(c, pin_key_cb, true);
    pin_refresh(NULL, false);
}

static void lock_show(void)
{
    s_locked = true;
    if (s_lock) return;
    if (s_keyboard) lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    s_lock_entry[0] = 0;
    s_lock = box(s_root, LV_FLEX_FLOW_COLUMN, 10);
    lv_obj_add_flag(s_lock, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE); // nothing behind receives taps
    lv_obj_set_size(s_lock, lv_pct(100), lv_pct(100));
    bg(s_lock, C_BG, LV_OPA_COVER, 0);
    pad(s_lock, 24, 40);
    lv_obj_set_flex_align(s_lock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_lock_time = label(s_lock, "--:--", W_BOLD, 48, C_TEXT);
    s_lock_rec = label(s_lock, "", W_SEMIBOLD, 17, C_RED);
    lv_obj_t *sp = box(s_lock, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_height(sp, 20);
    symbol(s_lock, LV_SYMBOL_EYE_CLOSE, 28, C_MUTED);
    s_lock_msg = label(s_lock, T("lock.enter"), W_REGULAR, 17, C_MUTED);
    s_lock_dots = label(s_lock, " ", W_BOLD, 28, C_TEXT);
    keypad(s_lock, lock_key_cb, false);
}

// Called every cycle: locks on inactivity, keeps the lock screen on top and updates it.
static void lock_tick(const app_status_t *st)
{
    if (!app_lock_enabled()) {
        if (s_locked) lock_hide();
        return;
    }
    if (!s_locked && lv_display_get_inactive_time(NULL) > (uint32_t)app_lock_after_s() * 1000) lock_show();
    if (!s_lock) return;
    if (!s_wake_overlay) lv_obj_move_foreground(s_lock);
    lv_label_set_text(s_lock_time, lv_label_get_text(s_clock));
    if (st->recording) {
        char dur[16];
        fmt_duration(dur, sizeof(dur), st->rec_seconds);
        lv_label_set_text_fmt(s_lock_rec, T("lock.recording"), dur);
    } else {
        lv_label_set_text(s_lock_rec, "");
    }
    int32_t wait = (int32_t)(s_lock_wait_until - lv_tick_get());
    if (wait > 0) {
        char t[64];
        snprintf(t, sizeof(t), T("lock.wait"), (int)((wait + 999) / 1000));
        lv_label_set_text(s_lock_msg, t);
        lv_obj_set_style_text_color(s_lock_msg, lv_color_hex(C_RED), 0);
    }
}

static void build_settings(void)
{
    app_status_t st;
    app_get_status(&st);
    char ssid[33], space[40], gb[16];
    net_current_ssid(ssid);
    net_state_t ns = net_state();

    header(T("settings.title"), false, SCR_HOME);
    section(T("settings.connection"));
    lv_obj_set_style_pad_row(s_content, 9, 0); // the whole list fits without scrolling
    row(T("settings.wifi"), NULL, app_ap_enabled() ? T("settings.wifi.paused") : ns == NET_CONNECTED ? ssid : ns == NET_CONNECTING ? T("settings.wifi.connecting") : T("settings.wifi.disconnected"),
        LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_WIFI);
    row(T("ap.title"), NULL, app_ap_enabled() ? T("common.on_f") : T("common.off_f"), LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_AP);
    row(T("web.title"), NULL, !app_web_enabled() ? T("common.off") : ns == NET_CONNECTED || app_ap_enabled() ? T("common.on") : T("settings.web.no_network"), LV_SYMBOL_RIGHT, C_MUTED, go_cb,
        (void *)(intptr_t)SCR_WEB);
    row(T("dev.title"), NULL, app_dev_enabled() ? T("common.on") : T("common.off"), LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_DEV);
    section(T("settings.device"));
    char scr[40];
    snprintf(scr, sizeof(scr), "%s · %d%%", app_theme() ? T("display.theme.light") : T("display.theme.dark"), app_brightness());
    row(T("display.title"), NULL, scr, LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_DISPLAY);
    {
        i18n_lang_t langs[I18N_MAX_LANGS];
        int n = i18n_list(langs, I18N_MAX_LANGS);
        const char *name = i18n_current();
        for (int i = 0; i < n; i++) {
            if (strcmp(langs[i].code, i18n_current()) == 0) name = langs[i].name;
        }
        char nm[I18N_NAME_MAX + 1];
        strlcpy(nm, name, sizeof(nm));
        row(T("settings.language"), NULL, nm, LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_LANG);
    }
    char ntp[APP_NTP_MAX + 1];
    app_ntp_server(ntp);
    if (strlen(ntp) > 18) strcpy(ntp + 16, "…"); // the value must not push the title aside
    row(T("ntp.title"), NULL, ntp, LV_SYMBOL_RIGHT, C_MUTED, edit_go_cb, (void *)(intptr_t)EDIT_NTP);
    if (st.card_ok) {
        fmt_gb(gb, sizeof(gb), st.free_mb);
        snprintf(space, sizeof(space), T("settings.card.free"), gb);
    } else {
        strlcpy(space, st.card_on_host ? T("settings.card.on_host") : T("settings.card.none"), sizeof(space));
    }
    row(T("card.title"), NULL, space, LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_CARD);
    static const char *const FMT_NAMES[] = {"WAV", "Opus", "MP3"};
    row(T("settings.recording"), NULL, FMT_NAMES[app_rec_format()], LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_RECCFG);
    row(T("about.title"), NULL, "v" APP_VERSION, LV_SYMBOL_RIGHT, C_MUTED, go_cb, (void *)(intptr_t)SCR_ABOUT);
    // Slightly shorter rows here, so the whole list fits without scrolling.
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_content); i++) {
        lv_obj_t *ch = lv_obj_get_child(s_content, (int32_t)i);
        if (lv_obj_get_style_pad_top(ch, 0) == 14) lv_obj_set_style_pad_ver(ch, 11, 0);
    }
}

static net_ap_t s_aps[NET_MAX_APS];

static void ap_cb(lv_event_t *e)
{
    const net_ap_t *ap = &s_aps[(intptr_t)lv_event_get_user_data(e)];
    strlcpy(s_sel_ssid, ap->ssid, sizeof(s_sel_ssid));
    if (ap->saved) {
        s_user_connect = net_connect(ap->ssid, NULL) == ESP_OK;
        show(SCR_WIFI);
    } else if (!ap->secure) {
        s_user_connect = net_connect(ap->ssid, "") == ESP_OK;
        show(SCR_WIFI);
    } else {
        show(SCR_WIFI_PASS);
    }
}

static void rescan_cb(lv_event_t *e)
{
    (void)e;
    net_scan_start();
    lv_obj_clean(s_content);
    build_wifi();
}

static void retry_password_action(void)
{
    show(SCR_WIFI_PASS);
}

static void build_wifi(void)
{
    char ssid[33], line[80];
    net_current_ssid(ssid);
    s_wifi_state = net_state();

    if (app_ap_enabled()) {
        header(T("settings.wifi"), true, SCR_SETTINGS);
        wrapped(s_content, T("wifi.ap_on"), W_REGULAR, 18, C_MUTED, false);
        button(s_content, T("wifi.open_ap"), BTN_OUTLINE, NULL, go_cb, (void *)(intptr_t)SCR_AP);
        return;
    }
    lv_obj_t *h = header(T("settings.wifi"), true, SCR_SETTINGS);
    spacer(h);
    lv_obj_t *reload = box(h, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(reload, 52, 44);
    bg(reload, C_SURF, LV_OPA_COVER, 12);
    center(reload);
    symbol(reload, LV_SYMBOL_REFRESH, 20, C_TEXT);
    on_click(reload, rescan_cb, NULL);

    if (s_wifi_state == NET_CONNECTED) {
        section(T("wifi.connected"));
        row(ssid, T("wifi.connected.sub"), NULL, LV_SYMBOL_RIGHT, C_GREEN, go_cb, (void *)(intptr_t)SCR_NET_INFO);
    } else if (s_wifi_state == NET_CONNECTING) {
        snprintf(line, sizeof(line), T("wifi.connecting_to"), ssid);
        wrapped(s_content, line, W_SEMIBOLD, 17, C_AMBER, false);
    }

    section(T("wifi.available"));
    int n = net_scan_results(s_aps, NET_MAX_APS, &s_wifi_scan_done);
    if (!s_wifi_scan_done) {
        wrapped(s_content, T("wifi.scanning"), W_REGULAR, 17, C_MUTED, false);
    } else if (n == 0) {
        wrapped(s_content, T("wifi.none_found"), W_REGULAR, 17, C_MUTED, false);
    }
    for (int i = 0; i < n; i++) {
        if (s_wifi_state == NET_CONNECTED && strcmp(s_aps[i].ssid, ssid) == 0) continue;
        const char *kind = s_aps[i].saved ? T("wifi.saved") : s_aps[i].secure ? T("wifi.secured") : T("wifi.open");
        const char *signal = s_aps[i].rssi > -60 ? T("wifi.signal.strong") : s_aps[i].rssi > -75 ? T("wifi.signal.medium") : T("wifi.signal.weak");
        snprintf(line, sizeof(line), "%s · %s", kind, signal);
        row(s_aps[i].ssid, line, NULL, LV_SYMBOL_WIFI, C_MUTED, ap_cb, (void *)(intptr_t)i);
    }
    wrapped(s_content, T("wifi.hint"),
            W_REGULAR, 14, C_MUTED, false);
}

// ---------- Keyboard ----------
// Three modes, cycled by the bottom-left key, whose label shows the next mode:
// letters -> "123" -> "áç" -> "abc". Uppercase via the arrow key.

#define KEY (4 | LV_BUTTONMATRIX_CTRL_POPOVER)             // character key
#define KEY_FN(w) ((w) | LV_KEYBOARD_CTRL_BUTTON_FLAGS)     // function key
#define KEY_DEL (6 | LV_BUTTONMATRIX_CTRL_CHECKED)          // delete: repeats while held
#define KB_MODE_NUMBERS "123"
#define KB_MODE_ACCENTS "áç"
#define KB_MODE_LETTERS "abc"

static const char *const kb_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_NUMBERS, ",", " ", ".", LV_SYMBOL_OK, ""};
static const char *const kb_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_NUMBERS, ",", " ", ".", LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t kb_letters_ctrl[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY_FN(6), KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY_DEL,
    KEY_FN(3), 2, 8, 2, KEY_FN(3)};

static const char *const kb_numbers[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "@", "#", "$", "_", "&", "-", "+", "(", ")", "/", "\n",
    "*", "\"", "'", ":", ";", "!", "?", "%", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_ACCENTS, ",", " ", ".", LV_SYMBOL_OK, ""};
static const char *const kb_accents[] = {
    "á", "à", "â", "ã", "é", "ê", "í", "ó", "ô", "õ", "\n",
    "ú", "ü", "ç", "Á", "À", "Â", "Ã", "É", "Ê", "Í", "\n",
    "Ó", "Ô", "Õ", "Ú", "Ç", "=", "~", "^", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_LETTERS, ",", " ", ".", LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t kb_symbols_ctrl[] = {
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY,
    KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY, KEY_DEL,
    KEY_FN(3), 2, 8, 2, KEY_FN(3)};

static void keyboard_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *txt = lv_buttonmatrix_get_button_text(kb, id);
    if (!txt) return;

    if (strcmp(txt, LV_SYMBOL_UP) == 0) {
        bool lower = lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_LOWER;
        lv_keyboard_set_mode(kb, lower ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_TEXT_LOWER);
    } else if (strcmp(txt, KB_MODE_NUMBERS) == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    } else if (strcmp(txt, KB_MODE_ACCENTS) == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    } else if (strcmp(txt, KB_MODE_LETTERS) == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    } else {
        lv_keyboard_def_event_cb(e); // characters, delete and OK
    }
}

static void forget_action(void)
{
    char ssid[33];
    net_current_ssid(ssid);
    net_forget(ssid);
    show(SCR_WIFI);
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    dialog_ex(T("wifi.forget.title"), T("wifi.forget.text"),
              T("common.cancel"), T("wifi.forget.ok"), BTN_DANGER_FILLED, NULL, forget_action);
}

static void build_net_info(void)
{
    net_info_t info;
    header(T("net.title"), true, SCR_WIFI);
    if (!net_info(&info)) {
        wrapped(s_content, T("net.offline"), W_REGULAR, 18, C_MUTED, false);
        return;
    }
    char signal[40], channel[8];
    const char *quality = info.rssi > -60 ? T("net.quality.strong") : info.rssi > -75 ? T("net.quality.medium") : T("net.quality.weak");
    snprintf(signal, sizeof(signal), "%d dBm (%s)", info.rssi, quality);
    snprintf(channel, sizeof(channel), "%u", info.channel);

    lv_obj_t *t = label(s_content, info.ssid, W_BOLD, 30, C_TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 6);
    data_row(c, T("net.state"), T("net.state.connected"), C_GREEN);
    data_row(c, T("net.signal"), signal, C_TEXT);
    data_row(c, T("net.channel"), channel, C_TEXT);
    data_row(c, T("net.ip"), info.ip, C_TEXT);
    data_row(c, T("net.mask"), info.mask, C_TEXT);
    data_row(c, T("net.gateway"), info.gateway, C_TEXT);
    data_row(c, T("net.dns"), info.dns, C_TEXT);
    data_row(c, T("net.mac"), info.mac, C_TEXT);
    spacer(s_content);
    button(s_content, T("wifi.forget.button"), BTN_DANGER, NULL, forget_cb, NULL);
}

static void web_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_set_web_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    show(SCR_WEB);
}

static void edit_go_cb(lv_event_t *e)
{
    s_edit = (edit_t)(intptr_t)lv_event_get_user_data(e);
    s_edit_error = false;
    show(SCR_EDIT);
}

static void build_web(void)
{
    net_info_t info;
    bool enabled = app_web_enabled();
    header(T("web.title"), true, SCR_SETTINGS);

    lv_obj_t *toggle = card(s_content, LV_FLEX_FLOW_ROW, 12);
    pad(toggle, 16, 10);
    lv_obj_t *tl = label(toggle, T("web.allow"), W_SEMIBOLD, 18, C_TEXT);
    lv_obj_set_flex_grow(tl, 1);
    lv_obj_t *sw = lv_switch_create(toggle);
    lv_obj_set_size(sw, 64, 36);
    if (enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, web_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (!enabled) {
        wrapped(s_content, T("web.off_text"),
                W_REGULAR, 16, C_MUTED, false);
        return;
    }
    net_ap_info_t ap;
    net_ap_info(&ap);
    if (app_ap_enabled() && ap.on && ap.ip[0]) {
        // Own network: the address is its IP (.local names don't always work without a router).
        memset(&info, 0, sizeof(info));
        strlcpy(info.ip, ap.ip, sizeof(info.ip));
    } else if (!net_info(&info)) {
        wrapped(s_content, T("web.no_network"), W_REGULAR, 18,
                C_MUTED, false);
        return;
    }
    char url[40], user[APP_WEB_USER_MAX + 1], pass[APP_WEB_PASS_MAX + 1], host[APP_HOSTNAME_MAX + 1], named[APP_HOSTNAME_MAX + 16];
    snprintf(url, sizeof(url), "http://%s", info.ip);
    app_web_user(user);
    app_web_pass(pass);
    app_hostname(host);
    snprintf(named, sizeof(named), "http://%s.local", host);

    label(s_content, T("web.open_on"), W_REGULAR, 16, C_MUTED);
    // Tapping the address opens the network name (hostname) editor.
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_ROW, 12);
    lv_obj_t *addr = box(c, LV_FLEX_FLOW_COLUMN, 4);
    lv_obj_set_flex_grow(addr, 1);
    lv_obj_t *nl = label(addr, named, W_BOLD, 24, C_TEXT);
    lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nl, lv_pct(100));
    label(addr, url, W_SEMIBOLD, 18, C_MUTED);
    symbol(c, LV_SYMBOL_EDIT, 20, C_MUTED);
    on_click(c, go_cb, (void *)(intptr_t)SCR_HOSTNAME);

    section(T("web.sign_in"));
    row(T("common.user"), NULL, user, LV_SYMBOL_RIGHT, C_MUTED, edit_go_cb, (void *)(intptr_t)EDIT_WEB_USER);
    row(T("common.password"), NULL, pass, LV_SYMBOL_RIGHT, C_MUTED, edit_go_cb, (void *)(intptr_t)EDIT_WEB_PASS);

    lv_obj_t *qr_box = box(s_content, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_width(qr_box, lv_pct(100));
    center(qr_box);
    lv_obj_t *frame = box(qr_box, LV_FLEX_FLOW_COLUMN, 0);
    bg(frame, C_WHITE, LV_OPA_COVER, 12);
    pad(frame, 10, 10);
    lv_obj_t *qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, lv_color_hex(C_QR_DARK));
    lv_qrcode_set_light_color(qr, lv_color_hex(C_WHITE));
    lv_qrcode_update(qr, url, strlen(url));
    wrapped(s_content, T("web.qr_hint"), W_REGULAR, 15, C_MUTED, true);
}

// Single-line text field, in the UI style.
static lv_obj_t *text_field(const char *initial)
{
    lv_obj_t *ta = lv_textarea_create(s_content);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_style_text_font(ta, font(W_SEMIBOLD, 20), 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(C_SURF), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_radius(ta, 14, 0);
    lv_obj_set_style_pad_all(ta, 14, 0);
    lv_textarea_set_text(ta, initial);
    return ta;
}

// Custom keyboard (letters, digits, accents) bound to a text field. `on_ok` is
// called when the confirm key is tapped.
static void attach_keyboard(lv_obj_t *ta, lv_event_cb_t on_ok)
{
    // Full-width keyboard, docked at the bottom, outside the content margins.
    s_keyboard = lv_keyboard_create(s_root);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_keyboard, lv_pct(100), 390);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(C_SURF), 0);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_keyboard, 0, 0);
    lv_obj_set_style_radius(s_keyboard, 0, 0);
    lv_obj_set_style_pad_all(s_keyboard, 6, 0);
    lv_obj_set_style_pad_gap(s_keyboard, 6, 0);
    // Letters (accented too) use the Montserrat TTF; symbols like delete and OK fall back to the LVGL font.
    lv_font_t *key_font = (lv_font_t *)font(W_SEMIBOLD, 26);
    if (key_font != &lv_font_montserrat_20) key_font->fallback = &lv_font_montserrat_28;
    lv_obj_set_style_text_font(s_keyboard, key_font, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(C_SURF2), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_keyboard, 0, LV_PART_ITEMS);
    // Visual feedback: the tapped key turns green and shows an enlarged letter above the finger.
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(C_GREEN), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_keyboard, lv_color_hex(C_WHITE), LV_PART_ITEMS | LV_STATE_PRESSED);
    // Function keys (shift, delete, mode, OK) in a different shade from character keys.
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(s_pal->key_fn), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_lower, kb_letters_ctrl);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_upper, kb_letters_ctrl);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_SPECIAL, kb_numbers, kb_symbols_ctrl);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, kb_accents, kb_symbols_ctrl);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_event_cb(s_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_add_event_cb(s_keyboard, on_ok, LV_EVENT_READY, NULL);
}

static void rename_save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta || s_sel_rec >= s_rec_count) return;
    int id = s_recs[s_sel_rec].id;
    app_rename_recording(id, lv_textarea_get_text(s_pass_ta));
    // The file was renamed: reload the list and return to the same recording.
    s_rec_count = app_list_recordings(s_recs, MAX_RECORDINGS);
    for (int i = 0; i < s_rec_count; i++) {
        if (s_recs[i].id == id) s_sel_rec = i;
    }
    show(SCR_DETAIL);
}

static void build_rename(void)
{
    header(T("rename.title"), true, SCR_DETAIL);
    if (s_sel_rec >= s_rec_count) return;
    wrapped(s_content, T("rename.info"), W_REGULAR, 16, C_MUTED, false);
    s_pass_ta = text_field(s_recs[s_sel_rec].title);
    lv_textarea_set_max_length(s_pass_ta, 40);
    lv_textarea_set_placeholder_text(s_pass_ta, T("rename.placeholder"));
    wrapped(s_content, T("rename.rule"), W_REGULAR, 14, C_MUTED, false);
    button(s_content, T("common.save"), BTN_PRIMARY, NULL, rename_save_cb, NULL);
    attach_keyboard(s_pass_ta, rename_save_cb);
}

static void hostname_save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) return;
    if (app_set_hostname(lv_textarea_get_text(s_pass_ta))) {
        s_hostname_error = false;
        show(SCR_WEB);
    } else {
        s_hostname_error = true;
        show(SCR_HOSTNAME);
    }
}

static void build_hostname(void)
{
    char host[APP_HOSTNAME_MAX + 1];
    app_hostname(host);
    header(T("hostname.title"), true, SCR_WEB);
    wrapped(s_content, T("hostname.info"), W_REGULAR, 16, C_MUTED, false);

    s_pass_ta = text_field(host);
    lv_textarea_set_max_length(s_pass_ta, APP_HOSTNAME_MAX);
    lv_textarea_set_accepted_chars(s_pass_ta, "abcdefghijklmnopqrstuvwxyz0123456789-");
    wrapped(s_content, s_hostname_error ? T("hostname.invalid")
                                        : T("hostname.rule"),
            W_REGULAR, 14, s_hostname_error ? C_RED : C_MUTED, false);
    button(s_content, T("common.save"), BTN_PRIMARY, NULL, hostname_save_cb, NULL);
    attach_keyboard(s_pass_ta, hostname_save_cb);
}

// ---------- Generic single-field screen ----------

typedef struct {
    const char *title, *info, *rule;
    int max;
    screen_t back;
} edit_spec_t;

// title, info and rule are text keys (the language can change while the device is on).
static const edit_spec_t EDITS[] = {
    [EDIT_WEB_USER] = {"edit.web_user.title", "edit.web_user.info", "edit.web_user.rule", APP_WEB_USER_MAX, SCR_WEB},
    [EDIT_WEB_PASS] = {"edit.web_pass.title", "edit.web_pass.info", "edit.web_pass.rule", APP_WEB_PASS_MAX, SCR_WEB},
    [EDIT_AP_SSID] = {"edit.ap_ssid.title", "edit.ap_ssid.info", "edit.ap_ssid.rule", APP_AP_SSID_MAX, SCR_AP},
    [EDIT_AP_PASS] = {"edit.ap_pass.title", "edit.ap_pass.info", "edit.ap_pass.rule", APP_AP_PASS_MAX, SCR_AP},
    [EDIT_NTP] = {"ntp.title", "edit.ntp.info", "edit.ntp.rule", APP_NTP_MAX, SCR_SETTINGS},
};

static void edit_get(edit_t k, char *out)
{
    switch (k) {
    case EDIT_WEB_USER: app_web_user(out); break;
    case EDIT_WEB_PASS: app_web_pass(out); break;
    case EDIT_AP_SSID: app_ap_ssid(out); break;
    case EDIT_AP_PASS: app_ap_pass(out); break;
    case EDIT_NTP: app_ntp_server(out); break;
    }
}

static bool edit_set(edit_t k, const char *v)
{
    switch (k) {
    case EDIT_WEB_USER: return app_set_web_user(v);
    case EDIT_WEB_PASS: return app_set_web_pass(v);
    case EDIT_AP_SSID: return app_set_ap_ssid(v);
    case EDIT_AP_PASS: return app_set_ap_pass(v);
    case EDIT_NTP: return app_set_ntp_server(v);
    }
    return false;
}

static void edit_save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) return;
    if (edit_set(s_edit, lv_textarea_get_text(s_pass_ta))) {
        s_edit_error = false;
        show(EDITS[s_edit].back);
    } else {
        s_edit_error = true;
        show(SCR_EDIT);
    }
}

static void build_edit(void)
{
    const edit_spec_t *spec = &EDITS[s_edit];
    char cur[72];
    edit_get(s_edit, cur);
    header(T(spec->title), true, spec->back);
    wrapped(s_content, T(spec->info), W_REGULAR, 16, C_MUTED, false);
    s_pass_ta = text_field(cur);
    lv_textarea_set_max_length(s_pass_ta, spec->max);
    char msg[160];
    snprintf(msg, sizeof(msg), s_edit_error ? T("edit.rejected") : "%s", T(spec->rule));
    wrapped(s_content, msg, W_REGULAR, 14, s_edit_error ? C_RED : C_MUTED, false);
    if (s_edit == EDIT_NTP) {
        char when[64];
        strlcpy(when, T("ntp.never"), sizeof(when));
        time_t t = (time_t)net_last_time_sync();
        if (t) {
            struct tm tmv;
            localtime_r(&t, &tmv);
            snprintf(when, sizeof(when), T("ntp.last_sync"), tmv.tm_mday, month_abbr(tmv.tm_mon), tmv.tm_hour, tmv.tm_min);
        }
        wrapped(s_content, when, W_REGULAR, 14, t ? C_GREEN : C_MUTED, false);
    }
    button(s_content, T("common.save"), BTN_PRIMARY, NULL, edit_save_cb, NULL);
    attach_keyboard(s_pass_ta, edit_save_cb);
}

// ---------- Developer functions ----------

static void dev_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_set_dev_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    show(SCR_DEV);
}

static void build_dev(void)
{
    bool on = app_dev_enabled();
    header(T("dev.title"), true, SCR_SETTINGS);
    lv_obj_t *toggle = card(s_content, LV_FLEX_FLOW_ROW, 12);
    pad(toggle, 16, 10);
    lv_obj_t *tl = label(toggle, T("dev.functions"), W_SEMIBOLD, 18, C_TEXT);
    lv_obj_set_flex_grow(tl, 1);
    lv_obj_t *sw = lv_switch_create(toggle);
    lv_obj_set_size(sw, 64, 36);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, dev_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    wrapped(s_content, on ? T("dev.on_text") : T("dev.off_text"), W_REGULAR, 16,
            C_MUTED, false);
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 6);
    data_row(c, T("dev.firmware"), on ? T("dev.allowed") : T("dev.blocked"), on ? C_GREEN : C_MUTED);
    data_row(c, T("dev.log"), on ? T("dev.allowed") : T("dev.blocked"), on ? C_GREEN : C_MUTED);
    data_row(c, T("dev.screenshot"), on ? T("dev.allowed") : T("dev.blocked"), on ? C_GREEN : C_MUTED);
    wrapped(s_content, T("dev.footer"),
            W_REGULAR, 14, C_MUTED, false);
    char ver[32];
    snprintf(ver, sizeof(ver), T("dev.version"), APP_VERSION);
    label(s_content, ver, W_REGULAR, 14, C_MUTED);
}

// ---------- Own network (access point) ----------

static void ap_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_set_ap_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    show(SCR_AP);
}

static void ap_clients_text(char *out, size_t len, int n)
{
    if (n <= 0) strlcpy(out, T("ap.clients.none"), len);
    else if (n == 1) strlcpy(out, T("ap.clients.one"), len);
    else snprintf(out, len, T("ap.clients.many"), n);
}

// Wi-Fi QR text: WIFI:T:WPA;S:<name>;P:<password>;; with \ before \ ; , : "
static void wifi_qr_escape(char *out, size_t len, const char *in)
{
    size_t o = 0;
    for (; *in && o + 2 < len; in++) {
        if (strchr("\\;,:\"", *in)) out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = 0;
}

static void build_ap(void)
{
    bool on = app_ap_enabled();
    header(T("ap.title"), true, SCR_SETTINGS);

    lv_obj_t *toggle = card(s_content, LV_FLEX_FLOW_ROW, 12);
    pad(toggle, 16, 10);
    lv_obj_t *tl = label(toggle, T("ap.create"), W_SEMIBOLD, 18, C_TEXT);
    lv_obj_set_flex_grow(tl, 1);
    lv_obj_t *sw = lv_switch_create(toggle);
    lv_obj_set_size(sw, 64, 36);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, ap_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (!on) {
        wrapped(s_content, T("ap.off_text"),
                W_REGULAR, 16, C_MUTED, false);
        return;
    }

    char ssid[APP_AP_SSID_MAX + 1], pass[APP_AP_PASS_MAX + 1];
    app_ap_ssid(ssid);
    app_ap_pass(pass);
    net_ap_info_t info;
    net_ap_info(&info);

    // Join the network: name and password (tap to change) next to the QR for the phone camera.
    section(T("ap.join"));
    lv_obj_t *join = card(s_content, LV_FLEX_FLOW_ROW, 14);
    lv_obj_t *fields = box(join, LV_FLEX_FLOW_COLUMN, 12);
    lv_obj_set_flex_grow(fields, 1);
    const struct { const char *k, *v; edit_t e; } F[] = {{T("ap.network_name"), ssid, EDIT_AP_SSID}, {T("common.password"), pass, EDIT_AP_PASS}};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *f = box(fields, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_width(f, lv_pct(100));
        label(f, F[i].k, W_REGULAR, 14, C_MUTED);
        lv_obj_t *vr = box(f, LV_FLEX_FLOW_ROW, 6);
        lv_obj_set_width(vr, lv_pct(100));
        cross_center(vr);
        lv_obj_t *v = label(vr, F[i].v, W_SEMIBOLD, 18, C_TEXT);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(v, 1);
        symbol(vr, LV_SYMBOL_EDIT, 20, C_MUTED);
        on_click(f, edit_go_cb, (void *)(intptr_t)F[i].e);
    }
    label(fields, T("ap.qr_hint"), W_REGULAR, 14, C_MUTED);

    char es[2 * APP_AP_SSID_MAX + 1], ep[2 * APP_AP_PASS_MAX + 1], qrtext[2 * (APP_AP_SSID_MAX + APP_AP_PASS_MAX) + 32];
    wifi_qr_escape(es, sizeof(es), ssid);
    wifi_qr_escape(ep, sizeof(ep), pass);
    snprintf(qrtext, sizeof(qrtext), "WIFI:T:WPA;S:%s;P:%s;;", es, ep);
    lv_obj_t *frame = box(join, LV_FLEX_FLOW_COLUMN, 0);
    bg(frame, C_WHITE, LV_OPA_COVER, 10);
    pad(frame, 8, 8);
    lv_obj_t *qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr, 130);
    lv_qrcode_set_dark_color(qr, lv_color_hex(C_QR_DARK));
    lv_qrcode_set_light_color(qr, lv_color_hex(C_WHITE));
    lv_qrcode_update(qr, qrtext, strlen(qrtext));

    label(s_content, T("ap.then_open"), W_REGULAR, 16, C_MUTED);
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 10);
    char url[32];
    snprintf(url, sizeof(url), "http://%s", info.ip[0] ? info.ip : "192.168.4.1");
    label(c, url, W_BOLD, 24, C_TEXT);
    if (app_web_enabled()) {
        char n[40], user[APP_WEB_USER_MAX + 1], wpass[APP_WEB_PASS_MAX + 1];
        ap_clients_text(n, sizeof(n), info.clients);
        s_ap_clients = label(c, n, W_REGULAR, 15, C_MUTED);
        app_web_user(user);
        app_web_pass(wpass);
        data_row(c, T("common.user"), user, C_TEXT);
        data_row(c, T("common.password"), wpass, C_GREEN);
    } else {
        label(c, T("ap.web_off"), W_REGULAR, 15, C_AMBER);
    }
    button(s_content, T("ap.web_settings"), BTN_OUTLINE, NULL, go_cb, (void *)(intptr_t)SCR_WEB);
}

static void connect_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) return;
    s_user_connect = net_connect(s_sel_ssid, lv_textarea_get_text(s_pass_ta)) == ESP_OK;
    show(SCR_WIFI);
}

static void build_wifi_pass(void)
{
    header(s_sel_ssid, true, SCR_WIFI);
    label(s_content, T("wifi.pass.label"), W_REGULAR, 16, C_MUTED);

    s_pass_ta = text_field("");

    button(s_content, T("wifi.pass.connect"), BTN_PRIMARY, NULL, connect_cb, NULL);

    attach_keyboard(s_pass_ta, connect_cb);
}

static void format_action(void)
{
    bool ok = app_format_card();
    strlcpy(s_toast_title, ok ? T("card.formatted") : T("card.format_failed"), sizeof(s_toast_title));
    strlcpy(s_toast_sub, ok ? T("card.formatted.sub") : T("card.format_failed.sub"), sizeof(s_toast_sub));
    s_toast_until = lv_tick_get() + TOAST_MS;
    show(SCR_HOME);
}

// Unrecognized card: confirm before erasing, since the device doesn't know what's on it.
static void format_unreadable_cb(lv_event_t *e)
{
    (void)e;
    dialog(T("notice.unreadable.confirm.title"), T("notice.unreadable.confirm.text"), T("card.format.ok"), format_action);
}

static void format_cb(lv_event_t *e)
{
    (void)e;
    char text[140];
    int n = app_list_recordings(s_recs, MAX_RECORDINGS);
    s_rec_count = n;
    if (n > 0) {
        snprintf(text, sizeof(text), n == 1 ? T("card.format.text.one") : T("card.format.text.many"), n);
    } else {
        strlcpy(text, T("card.format.text.empty"), sizeof(text));
    }
    dialog(T("card.format.title"), text, T("card.format.ok"), format_action);
}

static void build_card(void)
{
    app_status_t st;
    app_get_status(&st);
    header(T("card.title"), true, SCR_SETTINGS);
    if (!st.card_ok) {
        wrapped(s_content, st.card_on_host ? T("card.on_host") : T("card.none"), W_REGULAR, 18, C_MUTED, false);
        return;
    }
    char free_s[16], total_s[16], line[64];
    fmt_gb(free_s, sizeof(free_s), st.free_mb);
    fmt_gb(total_s, sizeof(total_s), st.total_mb);
    snprintf(line, sizeof(line), T("card.free_of"), free_s, total_s);
    label(s_content, line, W_SEMIBOLD, 22, C_TEXT);

    lv_obj_t *bar = lv_bar_create(s_content);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 12);
    lv_bar_set_range(bar, 0, 100);
    int used = st.total_mb ? (int)((uint64_t)(st.total_mb - st.free_mb) * 100 / st.total_mb) : 0;
    lv_bar_set_value(bar, used < 2 ? 2 : used, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_SURF2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);

    int n = app_list_recordings(s_recs, MAX_RECORDINGS);
    s_rec_count = n;
    char count[12], hours[32];
    snprintf(count, sizeof(count), "%d", n);
    snprintf(hours, sizeof(hours), T("card.hours"), (unsigned long)hours_for_mb(st.free_mb));
    lv_obj_t *c = card(s_content, LV_FLEX_FLOW_COLUMN, 0);
    pad(c, 16, 6);
    data_row(c, T("list.title"), count, C_TEXT);
    data_row(c, T("card.space"), hours, C_TEXT);

    spacer(s_content);
    wrapped(s_content, T("card.format.hint"), W_REGULAR, 14, C_MUTED, false);
    button(s_content, T("card.format.button"), BTN_DANGER, LV_SYMBOL_TRASH, format_cb, NULL);
}

// ---------- Navigation ----------

static void tab_cb(lv_event_t *e)
{
    app_status_t st;
    app_get_status(&st);
    if (st.recording) return;
    show((screen_t)(intptr_t)lv_event_get_user_data(e));
}

static void set_tab_color(lv_obj_t *item, uint32_t color)
{
    uint32_t n = lv_obj_get_child_count(item);
    for (uint32_t i = 0; i < n; i++) lv_obj_set_style_text_color(lv_obj_get_child(item, i), lv_color_hex(color), 0);
}

static void show(screen_t s)
{
    dialog_close();
    if (s_keyboard) lv_obj_delete(s_keyboard);
    s_keyboard = NULL;
    if (s != SCR_LIST) s_select_mode = false;
    s_cur = s;
    s_home_space = s_rec_timer = s_rec_bar = s_rec_file = s_rec_gain = s_pass_ta = s_ap_clients = s_about_update = NULL;
    lv_obj_clean(s_content);

    // Default: top-aligned, scrollable column.
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_content, 10, 0);
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);

    bool tabs_visible = s != SCR_REC && s != SCR_WIFI_PASS && s != SCR_HOSTNAME && s != SCR_EDIT && s != SCR_PIN && s != SCR_RENAME;
    if (tabs_visible) lv_obj_remove_flag(s_tabs, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_tabs, LV_OBJ_FLAG_HIDDEN);
    int active = s == SCR_LIST || s == SCR_DETAIL || s == SCR_RENAME ? 1 : s == SCR_SETTINGS || s == SCR_WIFI || s == SCR_WIFI_PASS || s == SCR_NET_INFO || s == SCR_WEB || s == SCR_HOSTNAME || s == SCR_EDIT || s == SCR_AP || s == SCR_DEV || s == SCR_DISPLAY || s == SCR_LANG || s == SCR_RECCFG || s == SCR_ABOUT || s == SCR_PIN || s == SCR_CARD ? 2 : 0;
    for (int i = 0; i < 3; i++) set_tab_color(s_tab_items[i], i == active ? C_GREEN : C_MUTED);

    switch (s) {
    case SCR_HOME: build_home(); break;
    case SCR_REC: build_rec(); break;
    case SCR_LIST: build_list(); break;
    case SCR_DETAIL: build_detail(); break;
    case SCR_RENAME: build_rename(); break;
    case SCR_SETTINGS: build_settings(); break;
    case SCR_WIFI: net_scan_start(); build_wifi(); break;
    case SCR_WIFI_PASS: build_wifi_pass(); break;
    case SCR_NET_INFO: build_net_info(); break;
    case SCR_WEB: s_hostname_error = false; build_web(); break;
    case SCR_HOSTNAME: build_hostname(); break;
    case SCR_EDIT: build_edit(); break;
    case SCR_AP: build_ap(); break;
    case SCR_DEV: build_dev(); break;
    case SCR_DISPLAY: build_display(); break;
    case SCR_LANG: build_lang(); break;
    case SCR_RECCFG: build_reccfg(); break;
    case SCR_ABOUT: build_about(); break;
    case SCR_PIN: build_pin(); break;
    case SCR_CARD: build_card(); break;
    case SCR_NOTICE: notice(s_notice); break;
    }
}

// ---------- Screen off while recording ----------

static void wake(void)
{
    display_backlight(true);
    if (s_wake_overlay) lv_obj_delete(s_wake_overlay);
    s_wake_overlay = NULL;
    s_screen_off = false;
    lv_display_trigger_activity(NULL);
}

static void wake_cb(lv_event_t *e)
{
    (void)e;
    wake(); // the tap that wakes the screen doesn't trigger anything underneath
}

static void lock_show(void);

static void sleep_screen(void)
{
    display_backlight(false);
    s_screen_off = true;
    if (app_lock_enabled()) lock_show(); // on wake, ask for the PIN
    s_wake_overlay = box(s_root, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_add_flag(s_wake_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_wake_overlay, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_wake_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wake_overlay, wake_cb, LV_EVENT_PRESSED, NULL);
}

// ---------- Periodic update ----------

static bool home_web_changed(void)
{
    char now[sizeof(s_home_web)], ip[HOME_SUB_LEN];
    home_web_text(now, sizeof(now), ip);
    return strcmp(now, s_home_web) != 0;
}

static void tick(lv_timer_t *t)
{
    (void)t;
    app_status_t st;
    app_get_status(&st);

    // Status bar
    if (net_time_valid()) {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        lv_label_set_text_fmt(s_clock, T("status.clock"), tmv.tm_mday, month_abbr(tmv.tm_mon), tmv.tm_hour, tmv.tm_min);
    } else {
        lv_label_set_text(s_clock, "--:--");
    }
    net_state_t ns = net_state();
    lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(ns == NET_CONNECTED ? C_TEXT : C_SURF2), 0);
    if (app_ap_enabled()) lv_obj_remove_flag(s_ap_icon, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ap_icon, LV_OBJ_FLAG_HIDDEN);
    if (s_cur == SCR_AP && s_ap_clients) {
        net_ap_info_t ai;
        net_ap_info(&ai);
        char n[40];
        ap_clients_text(n, sizeof(n), ai.clients);
        lv_label_set_text(s_ap_clients, n);
    }

    if (s_tour) return;

    char lang[APP_LANG_MAX + 1];
    app_language(lang);
    if (strcmp(lang, i18n_current()) != 0) {
        // Language change requested (UI or CONFIG.JSON): load it and rebuild everything with the new texts.
        if (!i18n_set(lang)) app_set_language(i18n_current()); // file missing or broken: keep the current one
        apply_theme();
        return;
    }
    if (app_theme() != s_theme_applied) {
        apply_theme();
        return;
    }

    // Core events
    if (st.event_seq != s_seen_event_seq) {
        s_seen_event_seq = st.event_seq;
        if (st.event == APP_EVENT_SAVED) {
            char dur[16];
            fmt_duration(dur, sizeof(dur), st.event_seconds);
            snprintf(s_toast_title, sizeof(s_toast_title), T("toast.saved.title"), dur);
            snprintf(s_toast_sub, sizeof(s_toast_sub), T("toast.saved.sub"), st.rec_name);
            s_toast_until = lv_tick_get() + TOAST_MS;
            if (s_cur == SCR_HOME) show(SCR_HOME);
        } else if (st.event == APP_EVENT_CARD_FULL || st.event == APP_EVENT_WRITE_ERROR) {
            s_notice = st.event == APP_EVENT_CARD_FULL ? NOTICE_CARD_FULL : NOTICE_WRITE_ERROR;
            s_notice_seconds = st.event_seconds;
            show(SCR_NOTICE);
        } else if (st.event == APP_EVENT_NO_CARD || st.event == APP_EVENT_CARD_BUSY) {
            show(SCR_HOME); // the home screen shows the matching notice
        }
    }

    // State-driven screen changes
    if (st.recording && s_cur != SCR_REC) {
        show(SCR_REC);
    } else if (!st.recording && s_cur == SCR_REC) {
        show(SCR_HOME);
    } else if (s_cur == SCR_HOME && (st.card_ok != s_home_card_ok || st.card_on_host != s_home_on_host || st.card_unreadable != s_home_unreadable)) {
        show(SCR_HOME);
    } else if (s_cur == SCR_HOME && st.card_ok && !st.card_on_host && !s_dialog && home_web_changed()) {
        show(SCR_HOME);
    } else if (s_cur == SCR_HOME && s_toast_title[0] && (int32_t)(lv_tick_get() - s_toast_until) > 0) {
        s_toast_title[0] = 0;
        show(SCR_HOME);
    } else if (s_cur == SCR_WIFI) {
        bool done;
        int found = net_scan_results(s_aps, NET_MAX_APS, &done);
        if (done != s_wifi_scan_done || ns != s_wifi_state) {
            // Scanning doesn't work during a connection attempt; redo it when that ends.
            if (s_wifi_state == NET_CONNECTING && ns != NET_CONNECTING && done && found == 0) net_scan_start();
            lv_obj_clean(s_content);
            build_wifi();
        }
    }

    if (s_cur == SCR_NET_INFO && ns != NET_CONNECTED) show(SCR_WIFI);

    // New firmware arriving over Wi-Fi: warn that the device will reboot.
    static bool update_shown;
    if (st.updating && !update_shown) {
        update_shown = true;
        dialog_ex(T("update.title"), T("update.text"),
                  NULL, T("common.ok"), BTN_PRIMARY, NULL, NULL);
    } else if (!st.updating) {
        update_shown = false;
    }

    // Card config file was read (applied or with errors).
    config_notice_t cfg_kind;
    int cfg_ignored;
    if (!s_dialog && !s_keyboard && config_take_notice(&cfg_kind, &cfg_ignored)) {
        if (s_cur == SCR_HOME || s_cur == SCR_SETTINGS || s_cur == SCR_WEB) show(s_cur); // show the new values on screen
        const char *cfg_title = T("config.title");
        char cfg_text[240];
        bool many = cfg_ignored > 1;
        switch (cfg_kind) {
        case CONFIG_NOTICE_UNREADABLE:
            cfg_title = T("config.unreadable.title");
            strlcpy(cfg_text, T("config.unreadable.text"), sizeof(cfg_text));
            break;
        case CONFIG_NOTICE_APPLIED_IGNORED:
            snprintf(cfg_text, sizeof(cfg_text), many ? T("config.applied_ignored.many") : T("config.applied_ignored.one"), cfg_ignored);
            break;
        case CONFIG_NOTICE_IGNORED:
            snprintf(cfg_text, sizeof(cfg_text), many ? T("config.ignored.many") : T("config.ignored.one"), cfg_ignored);
            break;
        default:
            strlcpy(cfg_text, T("config.applied"), sizeof(cfg_text));
            break;
        }
        dialog_ex(cfg_title, cfg_text, NULL, T("common.ok"), BTN_PRIMARY, NULL, NULL);
    }

    // Connection failure: popup, once, on any screen.
    if (ns == NET_FAILED) {
        char ssid[33], text[120];
        net_current_ssid(ssid);
        net_ack_failure();
        if (s_user_connect) {
            s_user_connect = false;
            strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));
            snprintf(text, sizeof(text), T("wifi.failed.text"), ssid);
            dialog_ex(T("wifi.failed.title"), text, T("common.close"), T("common.retry"), BTN_PRIMARY, NULL, retry_password_action);
        }
    } else if (ns == NET_CONNECTED) {
        s_user_connect = false;
    }

    // Values that change without switching screens
    if (s_cur == SCR_HOME && s_home_space) {
        char gb[16];
        fmt_gb(gb, sizeof(gb), st.free_mb);
        lv_label_set_text_fmt(s_home_space, T("home.space"), gb, (unsigned long)hours_for_mb(st.free_mb));
    }
    if (s_cur == SCR_REC && s_rec_timer) {
        char dur[16], size[16];
        fmt_duration(dur, sizeof(dur), st.rec_seconds);
        fmt_mb(size, sizeof(size), st.rec_bytes);
        lv_label_set_text(s_rec_timer, dur);
        int level = (int)st.level_dbfs;
        lv_bar_set_value(s_rec_bar, level < LEVEL_MIN_DB ? LEVEL_MIN_DB : level > 0 ? 0 : level, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_rec_bar, lv_color_hex(level > -3 ? C_RED : C_GREEN), LV_PART_INDICATOR);
        lv_label_set_text_fmt(s_rec_file, "%s · %s", st.rec_name, size);
        lv_label_set_text_fmt(s_rec_gain, "%d dB", st.gain_db);
    }

    lock_tick(&st);
    if (s_cur == SCR_ABOUT && s_about_update) {
        if (updater_state() != s_about_update_state) {
            if (!s_dialog) show(SCR_ABOUT); // new state: rebuild the row (icon and action change)
        } else if (updater_state() == UPD_DOWNLOADING) {
            char t[96];
            update_text(t, sizeof(t));
            lv_label_set_text(s_about_update, t);
        }
    }
    if (s_cur == SCR_ABOUT && s_about_progress) {
        int p = net_wifi_fw_update_progress();
        char t[64];
        if (p == -2) snprintf(t, sizeof(t), "%s", T("about.c6.failed"));
        else snprintf(t, sizeof(t), T("about.c6.progress"), p);
        lv_label_set_text(s_about_progress, t);
        if (p == -2) app_set_updating(false);
    }

    // Screen off while recording
    int to = app_screen_timeout_s();
    if (st.recording && to > 0 && !s_screen_off && lv_display_get_inactive_time(NULL) > (uint32_t)to * 1000) {
        sleep_screen();
    } else if (!st.recording && s_screen_off) {
        wake();
    }
}

// ---------- Captures for review ----------

#if UI_TOUR
static void snapshot(const char *name)
{
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!buf) {
        printf("SNAP-ERRO %s\n", name);
        return;
    }
    // Half resolution, so sending over serial is fast.
    int w = buf->header.w / 2, h = buf->header.h / 2;
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    printf("\nSNAP %s %d %d\n", name, w, h);
    uint8_t line[240 * 2 + 3];
    char out[((240 * 2 + 2) / 3) * 4 + 2];
    for (int y = 0; y < h; y++) {
        const uint8_t *src = buf->data + (size_t)(y * 2) * buf->header.stride;
        int n = 0;
        for (int x = 0; x < w && n < 480; x++) {
            line[n++] = src[x * 4];
            line[n++] = src[x * 4 + 1];
        }
        int o = 0;
        for (int i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)line[i] << 16 | (uint32_t)(i + 1 < n ? line[i + 1] : 0) << 8 | (i + 2 < n ? line[i + 2] : 0);
            out[o++] = b64[v >> 18 & 63];
            out[o++] = b64[v >> 12 & 63];
            out[o++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
            out[o++] = i + 2 < n ? b64[v & 63] : '=';
        }
        out[o] = 0;
        puts(out);
    }
    printf("ENDSNAP\n");
    fflush(stdout);
    lv_draw_buf_destroy(buf);
}

static void tour_step(const char *name, screen_t s, void (*after)(void))
{
    if (display_lock()) {
        show(s);
        if (after) after();
        display_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(s == SCR_WIFI ? 5000 : 700));
    if (display_lock()) {
        if (s == SCR_WIFI) {
            lv_obj_clean(s_content);
            build_wifi();
            lv_refr_now(NULL);
        }
        snapshot(name);
        display_unlock();
    }
}

static void tour_toast(void)
{
    strlcpy(s_toast_title, "Recording saved · 12:51", sizeof(s_toast_title));
    strlcpy(s_toast_sub, "REC_0023.WAV on the card", sizeof(s_toast_sub));
    lv_obj_clean(s_content);
    build_home();
}

static void tour_delete_dialog(void) { delete_cb(NULL); }
static void tour_format_dialog(void) { format_cb(NULL); }
static void tour_full(void) { s_notice = NOTICE_CARD_FULL; s_notice_seconds = 2832; lv_obj_clean(s_content); notice(s_notice); }
static void tour_usb(void) { lv_obj_clean(s_content); notice(NOTICE_USB); }
static void tour_no_card(void) { lv_obj_clean(s_content); notice(NOTICE_NO_CARD); }

static void tour_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(7000)); // let the network come up
    s_tour = true;
    strlcpy(s_sel_ssid, "MinhaRede", sizeof(s_sel_ssid));
    tour_step("01-inicio", SCR_HOME, NULL);
    tour_step("02-gravando", SCR_REC, NULL);
    tour_step("03-inicio-salvo", SCR_HOME, tour_toast);
    tour_step("04-gravacoes", SCR_LIST, NULL);
    tour_step("05-detalhe", SCR_DETAIL, NULL);
    tour_step("06-apagar", SCR_DETAIL, tour_delete_dialog);
    tour_step("07-ajustes", SCR_SETTINGS, NULL);
    tour_step("08-wifi", SCR_WIFI, NULL);
    tour_step("09-wifi-senha", SCR_WIFI_PASS, NULL);
    tour_step("10-cartao", SCR_CARD, NULL);
    tour_step("11-formatar", SCR_CARD, tour_format_dialog);
    tour_step("12-pendrive", SCR_NOTICE, tour_usb);
    tour_step("13-sem-cartao", SCR_NOTICE, tour_no_card);
    tour_step("14-cartao-cheio", SCR_NOTICE, tour_full);
    printf("TOUR-FIM\n");
    if (display_lock()) {
        s_toast_title[0] = 0;
        s_tour = false;
        show(SCR_HOME);
        display_unlock();
    }
    vTaskDelete(NULL);
}
#endif

// ---------- Web capture ----------

static bool screen_by_name(const char *name)
{
    static const struct {
        const char *name;
        screen_t s;
        int edit; // -1: not SCR_EDIT
    } MAP[] = {
        {"inicio", SCR_HOME, -1},       {"gravacoes", SCR_LIST, -1},
        {"ajustes", SCR_SETTINGS, -1},  {"wifi", SCR_WIFI, -1},
        {"navegador", SCR_WEB, -1},     {"rede-propria", SCR_AP, -1},
        {"cartao", SCR_CARD, -1},       {"hora", SCR_EDIT, EDIT_NTP},
        {"desenvolvedor", SCR_DEV, -1}, {"codigo", SCR_PIN, -1}, {"tela", SCR_DISPLAY, -1}, {"idioma", SCR_LANG, -1},
        {"gravacao", SCR_RECCFG, -1}, {"sobre", SCR_ABOUT, -1},
        {"nome-rede-propria", SCR_EDIT, EDIT_AP_SSID},
        {"senha-rede-propria", SCR_EDIT, EDIT_AP_PASS},
    };
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (strcmp(name, MAP[i].name) == 0) {
            if (MAP[i].edit >= 0) {
                s_edit = (edit_t)MAP[i].edit;
                s_edit_error = false;
            }
            show(MAP[i].s);
            return true;
        }
    }
    return false;
}

bool ui_capture_bmp(const char *screen, uint8_t **bmp, size_t *len)
{
    *bmp = NULL;
    *len = 0;
    if (!display_lock()) return false;
    if (screen && screen[0]) screen_by_name(screen);
    lv_refr_now(NULL);
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bool ok = false;
    if (buf) {
        int w = buf->header.w, h = buf->header.h;
        size_t row = ((size_t)w * 3 + 3) & ~(size_t)3;
        size_t total = 54 + row * h;
        uint8_t *out = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
        if (out) {
            memset(out, 0, 54);
            out[0] = 'B';
            out[1] = 'M';
            uint32_t v;
            v = total; memcpy(out + 2, &v, 4);
            v = 54; memcpy(out + 10, &v, 4);
            v = 40; memcpy(out + 14, &v, 4);
            v = w; memcpy(out + 18, &v, 4);
            v = h; memcpy(out + 22, &v, 4);
            uint16_t planes = 1, bpp = 24;
            memcpy(out + 26, &planes, 2);
            memcpy(out + 28, &bpp, 2);
            v = row * h; memcpy(out + 34, &v, 4);
            for (int y = 0; y < h; y++) {
                const uint8_t *src = buf->data + (size_t)y * buf->header.stride;
                uint8_t *dst = out + 54 + (size_t)(h - 1 - y) * row; // BMP stores rows bottom-up
                for (int x = 0; x < w; x++) {
                    uint16_t px = src[x * 2] | src[x * 2 + 1] << 8;
                    dst[x * 3] = (px & 0x1F) << 3;              // blue
                    dst[x * 3 + 1] = ((px >> 5) & 0x3F) << 2;   // green
                    dst[x * 3 + 2] = ((px >> 11) & 0x1F) << 3;  // red
                }
            }
            *bmp = out;
            *len = total;
            ok = true;
        }
        lv_draw_buf_destroy(buf);
    }
    display_unlock();
    return ok;
}

// ---------- Initialization ----------

// Status bar, content area and tabs, in the current theme's colors.
static void build_chrome(void)
{
    s_pal = app_theme() ? &PAL_LIGHT : &PAL_DARK;
    s_theme_applied = app_theme();
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_color_hex(C_GREEN), lv_color_hex(C_RED), s_pal->dark, &lv_font_montserrat_20);
    lv_display_set_theme(disp, theme);

    lv_obj_remove_style_all(s_root);
    bg(s_root, C_BG, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = box(s_root, LV_FLEX_FLOW_ROW, 10);
    lv_obj_set_size(status, lv_pct(100), 44);
    pad(status, 20, 0);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_clock = label(status, "--:--", W_SEMIBOLD, 16, C_MUTED);
    lv_obj_t *icons = box(status, LV_FLEX_FLOW_ROW, 10);
    cross_center(icons);
    s_ap_icon = label(icons, "AP", W_BOLD, 14, C_GREEN); // own network enabled
    lv_obj_add_flag(s_ap_icon, LV_OBJ_FLAG_HIDDEN);
    s_wifi_icon = symbol(icons, LV_SYMBOL_WIFI, 20, C_SURF2);

    s_content = box(s_root, LV_FLEX_FLOW_COLUMN, 10);
    lv_obj_set_width(s_content, lv_pct(100));
    lv_obj_set_flex_grow(s_content, 1);
    pad(s_content, 24, 12);

    s_tabs = box(s_root, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(s_tabs, lv_pct(100), 76);
    bg(s_tabs, C_SURF, LV_OPA_COVER, 0);
    static const struct { const char *name; const char *sym; screen_t scr; } tabs[3] = {
        {"tab.home", LV_SYMBOL_HOME, SCR_HOME}, // text keys
        {"list.title", LV_SYMBOL_LIST, SCR_LIST},
        {"settings.title", LV_SYMBOL_SETTINGS, SCR_SETTINGS},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *it = box(s_tabs, LV_FLEX_FLOW_COLUMN, 4);
        lv_obj_set_flex_grow(it, 1);
        lv_obj_set_height(it, lv_pct(100));
        center(it);
        symbol(it, tabs[i].sym, 20, C_MUTED);
        label(it, T(tabs[i].name), W_SEMIBOLD, 14, C_MUTED);
        on_click(it, tab_cb, (void *)(intptr_t)tabs[i].scr);
        s_tab_items[i] = it;
    }

}

// Theme change: clear everything and rebuild, returning to the same screen.
static void apply_theme(void)
{
    s_dialog = NULL;   // the objects go away with the cleanup below
    s_keyboard = NULL;
    s_wake_overlay = NULL;
    s_screen_off = false;
    s_lock = NULL;
    lv_obj_clean(s_root);
    build_chrome();
    show(s_cur);
    if (s_locked) {
        s_locked = false;
        lock_show();
    }
}

void ui_init(void)
{
    if (!display_lock()) return;

    s_root = lv_screen_active();
    build_chrome();

    app_status_t st;
    app_get_status(&st);
    s_seen_event_seq = st.event_seq;
    show(SCR_HOME);
    if (app_lock_enabled()) lock_show(); // enabled: ask for the PIN
    lv_timer_create(tick, 200, NULL);

    display_unlock();

#if UI_TOUR
    xTaskCreate(tour_task, "tour", 8192, NULL, 2, NULL);
#endif
}
