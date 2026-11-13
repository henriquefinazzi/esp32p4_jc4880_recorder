// Configurações do gravador em JSON (arquivo CONFIG.JSON no cartão).
// C puro, sem nada do ESP-IDF além do cJSON: testado no computador (test_host).
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
    int screen_off_s; // 0, 10, 30 ou 60 (0 = nunca)
    int gain_db;
    int theme;        // 0 escuro, 1 claro
    int rec_format;   // 0 wav, 1 opus, 2 mp3
    bool filter;      // filtro de ruído grave
    bool autodelete;  // cartão cheio: apagar as gravações mais antigas
    bool lock_on;     // bloqueio de tela com código
    char lock_pin[SETTINGS_LOCK_PIN_MAX + 1];
    int lock_after_s; // 60, 300 ou 900
    char lang[SETTINGS_LANG_MAX + 1]; // código do idioma: pt, en ou um arquivo do cartão
    bool web_on;
    bool dev_on;      // funções de desenvolvedor pela web (firmware, registro, captura)
    char host[SETTINGS_HOST_MAX + 1];
    char user[SETTINGS_USER_MAX + 1];
    char pass[SETTINGS_PASS_MAX + 1];
    int wifi_count;   // em ordem de preferência
    settings_wifi_t wifi[SETTINGS_WIFI_MAX];
    char ntp[SETTINGS_NTP_MAX + 1];           // servidor de hora
    bool ap_on;                               // rede própria do gravador
    char ap_ssid[SETTINGS_SSID_MAX + 1];
    char ap_pass[SETTINGS_AP_PASS_MAX + 1];
} settings_t;

// Regras de cada valor (as mesmas usadas pela tela).
bool settings_host_valid(const char *s);
bool settings_user_valid(const char *s);
bool settings_pass_valid(const char *s);
bool settings_wifi_valid(const char *ssid, const char *pass);
bool settings_lang_valid(const char *s);   // código de idioma: 2 a 15 letras minúsculas, números ou "-"
bool settings_ntp_valid(const char *s);
bool settings_lock_pin_valid(const char *s); // 4 a 8 dígitos     // nome ou IP: letras, números, ponto e hífen
bool settings_ap_ssid_valid(const char *s); // 1 a 32 bytes, sem caracteres de controle
bool settings_ap_pass_valid(const char *s); // WPA2: 8 a 63 caracteres ASCII visíveis

// Texto JSON indentado. Devolve memória de malloc (liberar com free) ou NULL.
char *settings_to_json(const settings_t *s);

// Lê `text` por cima de `current`: o que faltar ou for inválido fica como está em
// `current`, e cada valor inválido conta em `*ignored`. false se o texto não for um JSON
// com um objeto na raiz (aí `out` recebe `current` sem mudança).
bool settings_from_json(const char *text, const settings_t *current, settings_t *out, int *ignored);

// true se as duas configurações forem iguais.
bool settings_equal(const settings_t *a, const settings_t *b);
