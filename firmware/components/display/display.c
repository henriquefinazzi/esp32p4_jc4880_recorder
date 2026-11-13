#include "display.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st7701_init.h"

#define PIN_LCD_RST 5
#define PIN_BACKLIGHT 23
#define PIN_TOUCH_RST 3

// PWM backlight. 20 kHz is above the audible range, so it does not show up in recordings.
#define BL_FREQ_HZ 20000
#define BL_DUTY_BITS LEDC_TIMER_10_BIT
#define BL_DUTY_MAX ((1 << 10) - 1)

#define DSI_LDO_CHANNEL 3
#define DSI_LDO_MV 2500
#define DSI_LANES 2
#define DSI_LANE_MBPS 500
#define DPI_CLOCK_MHZ 34

static const char *TAG = "display";

static bool s_bl_ready, s_bl_on;
static int s_bl_percent = 100;

static esp_lcd_panel_io_handle_t s_dbi_io;
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;

static esp_err_t panel_init(void)
{
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {.chan_id = DSI_LDO_CHANNEL, .voltage_mv = DSI_LDO_MV};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "DSI LDO");

    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

    esp_lcd_dbi_io_config_t dbi_cfg = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &s_dbi_io), TAG, "command channel");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLOCK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = DISPLAY_H_RES,
            .v_size = DISPLAY_V_RES,
            .hsync_pulse_width = 12,
            .hsync_back_porch = 42,
            .hsync_front_porch = 42,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 8,
            .vsync_front_porch = 166,
        },
        .flags.use_dma2d = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(bus, &dpi_cfg, &s_panel), TAG, "DPI panel");

    // Panel reset after DPI and before the command sequence.
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < BOARD_P4_PANEL_INIT_CMDS_SIZE; i++) {
        const board_p4_panel_init_cmd_t *c = &board_p4_panel_init_cmds[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_dbi_io, c->cmd, c->data, c->len), TAG,
                            "command 0x%02X", c->cmd);
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out)
{
    i2c_master_bus_handle_t i2c = NULL;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&i2c), TAG, "i2c");

    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t addr;
    if (i2c_master_probe(i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) == ESP_OK) {
        addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    } else if (i2c_master_probe(i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK) {
        addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "GT911 at 0x%02X", addr);

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = addr;
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c, &io_cfg, &io), TAG, "touch io");

    esp_lcd_touch_config_t cfg = {
        .x_max = DISPLAY_H_RES,
        .y_max = DISPLAY_V_RES,
        .rst_gpio_num = -1, // already reset above
        .int_gpio_num = -1, // polled reads
    };
    return esp_lcd_touch_new_i2c_gt911(io, &cfg, out);
}

esp_err_t display_init(void)
{
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = BL_DUTY_BITS,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "backlight pwm");
    const ledc_channel_config_t bl_channel = {
        .gpio_num = PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "backlight channel");
    s_bl_ready = true;

    ESP_RETURN_ON_ERROR(panel_init(), TAG, "panel");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 32 * 1024; // TTF font rendering uses a lot of stack
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_dbi_io,
        .panel_handle = s_panel,
        .buffer_size = DISPLAY_H_RES * 100,
        .double_buffer = false,
        .hres = DISPLAY_H_RES,
        .vres = DISPLAY_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_spiram = true},
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {.flags = {.avoid_tearing = false}};
    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "lvgl display");

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t err = touch_init(&touch);
    if (err == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = s_disp, .handle = touch};
        if (!lvgl_port_add_touch(&touch_cfg)) ESP_LOGW(TAG, "touch not registered in lvgl");
    } else {
        ESP_LOGW(TAG, "touch unavailable (%s)", esp_err_to_name(err));
    }

    display_backlight(true);
    return ESP_OK;
}

static void backlight_apply(void)
{
    if (!s_bl_ready) return;
    uint32_t duty = s_bl_on ? (uint32_t)s_bl_percent * BL_DUTY_MAX / 100 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_backlight(bool on)
{
    s_bl_on = on;
    backlight_apply();
}

void display_set_brightness(int percent)
{
    if (percent < DISPLAY_BRIGHTNESS_MIN) percent = DISPLAY_BRIGHTNESS_MIN;
    if (percent > 100) percent = 100;
    s_bl_percent = percent;
    backlight_apply();
}

bool display_lock(void)
{
    return lvgl_port_lock(0);
}

void display_unlock(void)
{
    lvgl_port_unlock();
}
