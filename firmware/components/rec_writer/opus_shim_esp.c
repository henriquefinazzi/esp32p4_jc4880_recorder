// opus_shim using Espressif's Opus encoder (esp_audio_codec).
#include <stdlib.h>

#include "encoder/impl/esp_opus_enc.h"
#include "opus_shim.h"

struct opus_shim {
    void *enc;
    int in_bytes;
};

opus_shim_t *opus_shim_open(uint32_t sample_rate, uint32_t bitrate)
{
    opus_shim_t *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    esp_opus_enc_config_t cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = (int)sample_rate;
    cfg.channel = ESP_AUDIO_MONO;
    cfg.bits_per_sample = 16;
    cfg.bitrate = (int)bitrate;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    cfg.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP; // speech: intelligibility first
    cfg.complexity = 5;
    cfg.enable_vbr = false; // constant bitrate: duration is derived from the file size
    int out_bytes = 0;
    if (esp_opus_enc_open(&cfg, sizeof(cfg), &e->enc) != ESP_AUDIO_ERR_OK ||
        esp_opus_enc_get_frame_size(e->enc, &e->in_bytes, &out_bytes) != ESP_AUDIO_ERR_OK ||
        e->in_bytes != (int)(sample_rate / 50 * sizeof(int16_t))) {
        if (e->enc) esp_opus_enc_close(e->enc);
        free(e);
        return NULL;
    }
    return e;
}

int opus_shim_encode(opus_shim_t *e, const int16_t *pcm, uint8_t *out, size_t out_max)
{
    esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)pcm, .len = (uint32_t)e->in_bytes};
    esp_audio_enc_out_frame_t o = {.buffer = out, .len = (uint32_t)out_max};
    if (esp_opus_enc_process(e->enc, &in, &o) != ESP_AUDIO_ERR_OK) return -1;
    return (int)o.encoded_bytes;
}

void opus_shim_close(opus_shim_t *e)
{
    esp_opus_enc_close(e->enc);
    free(e);
}
