// Recorder user interface. Reads state and requests actions through app.h.
#pragma once

// Builds the interface. Call once, after display_init().
void ui_init(void);

// Screen capture as a 24-bit BMP (480x800), to check the interface from the browser.
// `screen` (optional) first opens a screen by name: inicio, gravacoes, ajustes, wifi,
// navegador, rede-propria, hora, nome-rede-propria, senha-rede-propria, cartao.
// Returns malloc'd memory in *bmp (release with free). false if the capture fails.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool ui_capture_bmp(const char *screen, uint8_t **bmp, size_t *len);
