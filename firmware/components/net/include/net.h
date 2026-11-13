// Wi-Fi do gravador (ESP32-C6 via ESP-Hosted): redes salvas, varredura, conexão e hora.
// Só redes de 2,4 GHz com senha comum ou abertas.
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

// Liga o rádio e tenta as redes salvas em segundo plano. Precisa do NVS já iniciado.
esp_err_t net_init(void);

net_state_t net_state(void);

// Rede em uso ou em tentativa; string vazia se nenhuma.
void net_current_ssid(char out[33]);

// Varredura em segundo plano. `net_scan_results` devolve o total (0 enquanto procura)
// e põe em `done` se a varredura terminou.
void net_scan_start(void);
int net_scan_results(net_ap_t *out, int max, bool *done);

// Conecta sem bloquear. `password` NULL usa a senha salva dessa rede.
// A rede só é salva depois de conectar com sucesso.
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

// Dados da conexão atual. false se não estiver conectado.
bool net_info(net_info_t *out);

// Esquece uma rede salva e, se for a atual, desconecta.
void net_forget(const char *ssid);

typedef struct {
    char ssid[33];
    char password[65];
} net_saved_t;

// Redes salvas, da preferida para a menos usada. Devolve quantas preencheu.
// Funciona antes de net_init (só precisa do NVS).
int net_saved_list(net_saved_t *out, int max);

// Substitui a lista inteira (até NET_MAX_SAVED, na ordem dada). Se a rede em uso sair
// da lista, desconecta.
void net_saved_set(const net_saved_t *list, int n);

// Chamada sempre que a lista de redes salvas pode ter mudado (fora da tarefa da tela;
// deve só marcar algo e voltar).
void net_on_saved_changed(void (*cb)(void));

// Depois de mostrar uma falha ao usuário: volta NET_FAILED para NET_IDLE.
void net_ack_failure(void);

// true depois que o relógio foi acertado pela internet.
bool net_time_valid(void);

// Acerta o relógio com uma hora vinda de fora (ex.: o navegador), só se a hora ainda não
// veio do servidor NTP. true se acertou.
bool net_set_time_from(long long epoch_s);

// Servidor de hora (NTP). Pode ser chamado antes de net_init; se a hora já estiver sendo
// buscada, troca de servidor na hora.
void net_set_ntp_server(const char *server);

// Momento (time_t) da última vez que a hora veio do servidor; 0 se ainda não veio.
long long net_last_time_sync(void);

// Rede própria do gravador (ponto de acesso). Ligada, o gravador sai da rede Wi-Fi em que
// estava (sem varredura, sem conexão nem hora pela internet) e volta a ela quando desligada.
// Pode ser chamado antes de net_init. `pass` com 8 a 63 caracteres (WPA2).
void net_ap_set(bool on, const char *ssid, const char *pass);

typedef struct {
    bool on;          // pedida e no ar
    char ip[16];      // endereço do gravador nesta rede
    int clients;      // aparelhos conectados
} net_ap_info_t;

void net_ap_info(net_ap_info_t *out);

// ---------- Chip de Wi-Fi (ESP32-C6, ESP-Hosted) ----------

// Versão do firmware que está no C6 ("2.3.0"). false se o C6 não respondeu.
bool net_wifi_fw_version(char out[24]);

// Versão do firmware do C6 que vem dentro deste firmware ("2.12.13").
const char *net_wifi_fw_bundled(void);

// Começa a atualizar o C6 com o firmware embutido, em segundo plano. No fim o aparelho
// reinicia. false se já houver uma atualização ou o C6 não estiver pronto.
bool net_wifi_fw_update_start(void);

// -1 parado; 0 a 100 enviando; -2 falhou (o C6 continua com a versão anterior).
int net_wifi_fw_update_progress(void);
