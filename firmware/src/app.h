// Fronteira entre o núcleo do gravador (main.c) e a interface (ui.c).
// A interface só lê estado e pede ações por aqui.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define APP_VERSION "0.4"
#define APP_GAIN_MIN_DB 0
#define APP_GAIN_MAX_DB 42
#define APP_GAIN_STEP_DB 6 // passos do pré-amplificador do ES8311

typedef enum {
    APP_EVENT_NONE,
    APP_EVENT_SAVED,      // gravação terminou e foi salva
    APP_EVENT_NO_CARD,    // tentou gravar sem cartão
    APP_EVENT_CARD_BUSY,  // tentou gravar com o cartão no computador
    APP_EVENT_CARD_FULL,  // faltou espaço durante a gravação (o que havia foi salvo)
    APP_EVENT_WRITE_ERROR // outro erro de escrita (o que havia foi salvo)
} app_event_t;

typedef struct {
    bool recording;
    uint32_t rec_seconds;
    uint32_t rec_bytes;
    char rec_name[16];   // "REC_0023"
    float level_dbfs;    // pico dos últimos 100 ms
    int gain_db;
    bool updating;       // recebendo firmware novo pelo Wi-Fi

    bool card_ok;        // cartão presente e com o aparelho
    bool card_on_host;   // cartão entregue ao computador pela USB
    bool card_unreadable; // cartão presente em formato que o aparelho não lê (exFAT, NTFS...)
    uint32_t total_mb;
    uint32_t free_mb;

    uint32_t event_seq;  // muda a cada novo evento
    app_event_t event;
    uint32_t event_seconds; // duração salva, para SAVED / CARD_FULL / WRITE_ERROR
} app_status_t;

#define APP_TITLE_MAX 60 // bytes do nome dado pelo usuário (UTF-8)

// Uma gravação. O arquivo é "REC_0023.WAV" ou, com nome, "REC_0023 Reunião cliente.WAV":
// o nome viaja com o arquivo (pendrive, download). O número identifica a gravação.
typedef struct {
    int id;                        // 23
    char file[96];                 // nome do arquivo no cartão
    char title[APP_TITLE_MAX + 1]; // "" se não tiver nome
    uint32_t bytes;
    uint32_t seconds;
    time_t mtime;                  // 0 se o relógio não estava acertado ao gravar
} app_recording_t;

void app_get_status(app_status_t *out);

void app_toggle_recording(void);

// Atualização de firmware em curso (pelo navegador): impede começar uma gravação.
void app_set_updating(bool updating);
void app_set_gain_db(int db);            // limita ao intervalo e guarda como padrão

int app_screen_timeout_s(void);          // 0 = nunca apagar
void app_set_screen_timeout_s(int seconds);

// Idioma da interface (código: pt, en ou um arquivo IDIOMAS/<código>.json do cartão).
#define APP_LANG_MAX 15
void app_language(char out[APP_LANG_MAX + 1]);
void app_set_language(const char *code); // a tela troca no próximo ciclo

// Formato das próximas gravações: 0 WAV, 1 Opus, 2 MP3 (ver rec_writer.h).
int app_rec_format(void);
void app_set_rec_format(int fmt);
// Bytes por segundo de gravação no formato escolhido (para estimar o tempo que cabe).
uint32_t app_rec_bytes_per_s(void);

// Filtro de ruído grave no áudio gravado.
bool app_filter_enabled(void);
void app_set_filter_enabled(bool on);

// Cartão cheio: apagar as gravações mais antigas para continuar gravando.
#define APP_AUTODELETE_MIN_MB 300 // espaço livre mantido durante a gravação
bool app_autodelete_enabled(void);
void app_set_autodelete_enabled(bool on);

// Bloqueio de tela com código numérico (4 a 8 dígitos). Liga só se houver código.
#define APP_LOCK_PIN_MAX 8
bool app_lock_enabled(void);
bool app_set_lock_enabled(bool on);          // false se não houver código
void app_lock_pin(char out[APP_LOCK_PIN_MAX + 1]);
bool app_set_lock_pin(const char *pin);      // false se inválido
int app_lock_after_s(void);                  // inatividade até travar: 60, 300 ou 900
void app_set_lock_after_s(int seconds);

// Tema da tela: 0 escuro, 1 claro.
int app_theme(void);
void app_set_theme(int theme);

int app_brightness(void);                // porcentagem
void app_set_brightness(int percent, bool save); // save=false enquanto o dedo arrasta o controle

// Mais recentes primeiro. Devolve quantas preencheu.
int app_list_recordings(app_recording_t *out, int max);
bool app_delete_recording(int id);
// Dá ou troca o nome ("" remove). Símbolos que o cartão não aceita em nomes são descartados.
bool app_rename_recording(int id, const char *title);
// Caminho completo do arquivo de uma gravação. false se não existir.
bool app_recording_path(int id, char *out, size_t len);
bool app_format_card(void);
bool app_retry_card(void);

// Usuário e senha do acesso pelo navegador. Na primeira vez que liga: usuário "recorder" e
// senha de 6 dígitos sorteada. Só caracteres ASCII visíveis; o usuário não pode ter ":".
#define APP_WEB_USER_MAX 32
#define APP_WEB_PASS_MIN 4
#define APP_WEB_PASS_MAX 32
void app_web_user(char out[APP_WEB_USER_MAX + 1]);
void app_web_pass(char out[APP_WEB_PASS_MAX + 1]);
bool app_set_web_user(const char *user); // false se inválido
bool app_set_web_pass(const char *pass); // false se inválido

// Nome do aparelho na rede local (http://<nome>.local). Só letras minúsculas, números e
// hífen, sem hífen nas pontas, de 1 a APP_HOSTNAME_MAX caracteres.
#define APP_HOSTNAME_MAX 24
void app_hostname(char out[APP_HOSTNAME_MAX + 1]);
bool app_set_hostname(const char *name); // false se o nome for inválido

// Servidor de hora (NTP), ex.: pool.ntp.org ou a.st1.ntp.br.
#define APP_NTP_MAX 63
void app_ntp_server(char out[APP_NTP_MAX + 1]);
bool app_set_ntp_server(const char *server); // false se inválido

// Rede própria do gravador (ponto de acesso). Nome e senha são sorteados na primeira vez.
#define APP_AP_SSID_MAX 32
#define APP_AP_PASS_MAX 63
bool app_ap_enabled(void);
void app_set_ap_enabled(bool on);
void app_ap_ssid(char out[APP_AP_SSID_MAX + 1]);
void app_ap_pass(char out[APP_AP_PASS_MAX + 1]);
bool app_set_ap_ssid(const char *ssid); // false se inválido
bool app_set_ap_pass(const char *pass); // false se inválido (8 a 63 caracteres)

// Funções de desenvolvedor pela rede: atualizar firmware, ler o registro, capturar a tela.
// Desligadas, esses endereços respondem que estão desativados.
bool app_dev_enabled(void);
void app_set_dev_enabled(bool on);

// Liga ou desliga o acesso pelo navegador. A escolha fica guardada.
bool app_web_enabled(void);
void app_set_web_enabled(bool enabled);
