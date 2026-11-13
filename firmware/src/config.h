// Espelho das configurações no cartão: /sdcard/CONFIG.JSON.
// A memória interna (NVS) continua sendo a fonte: sem cartão o aparelho liga configurado.
// - Toda mudança feita no aparelho regrava o arquivo (com um pequeno atraso).
// - Sempre que o cartão volta para o aparelho (ao ligar ou depois do modo pendrive), o
//   arquivo é lido: valores válidos que mudaram são aplicados; os inválidos são ignorados.
// - Arquivo com erro de sintaxe fica intocado até a próxima mudança feita no aparelho.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Depois do cartão, do áudio e da tela, antes do Wi-Fi e do servidor web.
void config_init(void);

// Avisa que alguma configuração mudou no aparelho.
void config_changed(void);

// Tipo de aviso sobre o arquivo lido; a tela monta o texto no idioma escolhido.
typedef enum {
    CONFIG_NOTICE_NONE,
    CONFIG_NOTICE_APPLIED,         // mudanças aplicadas
    CONFIG_NOTICE_APPLIED_IGNORED, // mudanças aplicadas, com valores inválidos ignorados
    CONFIG_NOTICE_IGNORED,         // nada mudou; só valores inválidos ignorados
    CONFIG_NOTICE_UNREADABLE,      // arquivo com erro de sintaxe, ignorado inteiro
} config_notice_t;

// Aviso pendente para a tela (arquivo aplicado ou com erro) e quantos valores foram
// ignorados. false se não houver.
bool config_take_notice(config_notice_t *kind, int *ignored);
