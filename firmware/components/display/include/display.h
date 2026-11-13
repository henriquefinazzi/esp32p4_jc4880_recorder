// Tela ST7701S (480x800, MIPI-DSI) + toque GT911 + LVGL da Guition JC4880P443C.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define DISPLAY_H_RES 480
#define DISPLAY_V_RES 800

// Liga o painel, o LVGL e o toque. Sem toque a tela ainda funciona (aviso no log).
esp_err_t display_init(void);

void display_backlight(bool on);

// Brilho da luz de fundo em porcentagem. O mínimo evita uma tela escura demais para achar o controle.
#define DISPLAY_BRIGHTNESS_MIN 10
void display_set_brightness(int percent);

// Toda chamada ao LVGL fora da tarefa dele fica entre lock e unlock.
bool display_lock(void);
void display_unlock(void);
