#include "board.h"

#define PIN_I2C_SDA 7
#define PIN_I2C_SCL 8

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_bus(i2c_master_bus_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_I2C_SDA,
            .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
        if (err != ESP_OK) return err;
    }
    *out = s_bus;
    return ESP_OK;
}
