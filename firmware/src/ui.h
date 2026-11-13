// Interface do gravador. Lê o estado e pede ações pelo app.h.
#pragma once

// Monta a interface. Chamar uma vez, depois de display_init().
void ui_init(void);

// Captura da tela em BMP de 24 bits (480x800), para conferir a interface pelo navegador.
// `screen` (opcional) abre antes uma tela pelo nome: inicio, gravacoes, ajustes, wifi,
// navegador, rede-propria, hora, nome-rede-propria, senha-rede-propria, cartao.
// Devolve memória de malloc em *bmp (liberar com free). false se a captura falhar.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool ui_capture_bmp(const char *screen, uint8_t **bmp, size_t *len);
