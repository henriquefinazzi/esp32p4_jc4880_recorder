// Shared resources of the Guition JC4880P443C.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// I2C bus 0 (SDA 7, SCL 8), shared by the GT911 touch and the ES8311 codec.
// Created on the first call; later calls return the same one.
esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out);
