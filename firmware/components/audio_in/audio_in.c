#include "audio_in.h"

#include "board.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

#define I2C_PORT I2C_NUM_0 // barramento compartilhado com o toque (ver board.h)

#define I2S_PORT I2S_NUM_0
#define PIN_I2S_MCLK 13
#define PIN_I2S_BCLK 12
#define PIN_I2S_LRCK 10
#define PIN_I2S_DOUT 9  // P4 -> codec (não usado para gravar)
#define PIN_I2S_DIN 48  // codec -> P4

static const char *TAG = "audio_in";

static esp_codec_dev_handle_t s_dev;

esp_err_t audio_in_init(uint32_t sample_rate_hz)
{
    if (s_dev) return ESP_OK;

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(board_i2c_bus(&i2c_bus), TAG, "i2c");

    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s canal");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din = PIN_I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");

    audio_codec_i2s_cfg_t i2s_if_cfg = {.port = I2S_PORT, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "interface i2s do codec");

    audio_codec_i2c_cfg_t i2c_if_cfg = {
        .port = I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_if_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "interface i2c do codec");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "interface gpio do codec");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
        .pa_pin = -1, // amplificador fica desligado
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_ERR_NOT_FOUND, TAG, "ES8311 nao respondeu no I2C");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(dev, ESP_FAIL, TAG, "codec dev");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate_hz,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(dev, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "abrir codec");

    s_dev = dev;
    return audio_in_set_gain_db(AUDIO_IN_DEFAULT_GAIN_DB);
}

esp_err_t audio_in_set_gain_db(float db)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_set_in_gain(s_dev, db) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

int audio_in_read(int16_t *samples, size_t n)
{
    if (!s_dev || !samples) return -1;
    if (esp_codec_dev_read(s_dev, samples, (int)(n * sizeof(int16_t))) != ESP_CODEC_DEV_OK) return -2;
    return (int)n;
}
