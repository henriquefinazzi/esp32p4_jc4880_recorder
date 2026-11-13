// ST7701S screen (480x800, MIPI-DSI) + GT911 touch + LVGL for the Guition JC4880P443C.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define DISPLAY_H_RES 480
#define DISPLAY_V_RES 800

// Starts the panel, LVGL and touch. Without touch the screen still works (warning in the log).
esp_err_t display_init(void);

void display_backlight(bool on);

// Backlight brightness in percent. The minimum avoids a screen too dark to find the control.
#define DISPLAY_BRIGHTNESS_MIN 10
void display_set_brightness(int percent);

// Every LVGL call outside its own task goes between lock and unlock.
bool display_lock(void);
void display_unlock(void);
