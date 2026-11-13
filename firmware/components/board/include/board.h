// Recursos compartilhados da Guition JC4880P443C.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// Barramento I2C 0 (SDA 7, SCL 8), compartilhado entre o toque GT911 e o codec ES8311.
// Cria na primeira chamada; as seguintes devolvem o mesmo.
esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out);
